// SPDX-License-Identifier: GPL-2.0-or-later
// The connection (task 0277). See BlendityLinkClient.h.
#include "BlendityLinkClient.h"

#include "Editor.h"
#include "IPAddress.h"
#include "Misc/EngineVersion.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
constexpr double BudgetSeconds = 0.020;
constexpr double WatchSeconds = 2.0;
constexpr double ConnectSeconds = 0.5;
}

FBlendityLinkClient::~FBlendityLinkClient()
{
	Disconnect();
}

bool FBlendityLinkClient::ConnectTo(int32 InPort, const FString& Scene)
{
	Disconnect(Status);
	if (InPort < 1 || InPort > 65535)
	{
		Status = TEXT("The port must be 1 to 65535.");
		return false;
	}
	ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	FSocket* S = Subsystem->CreateSocket(NAME_Stream, TEXT("Blendity Live Link"), false);
	if (!S)
	{
		Status = TEXT("Couldn't make a socket.");
		return false;
	}
	S->SetNonBlocking(true);
	S->SetNoDelay(true);
	TSharedRef<FInternetAddr> Addr = Subsystem->CreateInternetAddr();
	bool bValid = false;
	Addr->SetIp(TEXT("127.0.0.1"), bValid);
	Addr->SetPort(InPort);
	S->Connect(*Addr);  // (non-blocking: FinishConnect sees it through)
	Socket = S;
	bPending = true;
	PendingSince = FPlatformTime::Seconds();
	PendingScene = Scene;
	Port = InPort;
	Status = FString::Printf(TEXT("Connecting to 127.0.0.1:%d..."), InPort);
	return true;
}

void FBlendityLinkClient::Disconnect(const FString& Why)
{
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
	}
	Socket = nullptr;
	bPending = false;
	Status = Why;
}

void FBlendityLinkClient::Hold()
{
	Disconnect(TEXT("Disconnected (Connect to follow Blendity again)"));
	bHeld = true;
}

bool FBlendityLinkClient::TryAutoConnect()
{
	if (Socket)
		return true;
	blendity_link::Endpoint Found;
	const bool bFound = blendity_link::read_discovery(blendity_link::discovery_path(), Found);
	const int32 Use = ManualPort > 0 ? ManualPort : bFound ? Found.port : 0;
	if (Use == 0)
	{
		Status = TEXT("Waiting for Blendity... (start it, or Window > Live Link > Start)");
		return false;
	}
	return ConnectTo(Use, bFound ? FString(UTF8_TO_TCHAR(Found.scene.c_str())) : FString());
}

void FBlendityLinkClient::FinishConnect()
{
	const ESocketConnectionState State = Socket->GetConnectionState();
	if (State == SCS_Connected)
	{
		bPending = false;
		const std::string Hello = blendity_link::hello(TCHAR_TO_UTF8(*(TEXT("Unreal ") + FEngineVersion::Current().ToString(EVersionComponent::Patch))));
		int32 Sent = 0;
		Socket->Send((const uint8*)Hello.data(), (int32)Hello.size(), Sent);
		Stream = blendity_link::Stream();
		Applier = MakeUnique<FBlendityLinkApplier>(GEditor ? GEditor->GetEditorWorldContext().World() : nullptr);
		Messages = 0;
		bHeld = false;
		Status = PendingScene.IsEmpty() ? FString::Printf(TEXT("Connected to 127.0.0.1:%d"), Port) : FString::Printf(TEXT("Connected to Blendity (%s)"), *PendingScene);
		return;
	}
	if (State != SCS_ConnectionError && FPlatformTime::Seconds() - PendingSince < ConnectSeconds)
		return;
	Disconnect(bAutoConnect ? FString::Printf(TEXT("Waiting for Blendity... (port %d not answering)"), Port) : FString(TEXT("Couldn't connect. Start Live Link in Blendity first.")));
}

bool FBlendityLinkClient::ReadSome()
{
	uint32 Pending = 0;
	if (!Socket->HasPendingData(Pending))
	{
		if (Socket->GetConnectionState() != SCS_Connected)
			Disconnect(bAutoConnect ? TEXT("Blendity closed the connection - waiting for it again") : TEXT("Blendity closed the connection"));
		else
		{
			uint8 Peek = 0;
			int32 Got = 0;
			if (Socket->Recv(&Peek, 1, Got, ESocketReceiveFlags::Peek) && Got == 0)  // (a closed peer reads 0 bytes)
				Disconnect(bAutoConnect ? TEXT("Blendity closed the connection - waiting for it again") : TEXT("Blendity closed the connection"));
		}
		return false;
	}
	Chunk.SetNumUninitialized(FMath::Min<uint32>(Pending, 1u << 20));
	int32 Read = 0;
	if (!Socket->Recv(Chunk.GetData(), Chunk.Num(), Read) || Read <= 0)
	{
		Disconnect(TEXT("Blendity closed the connection"));
		return false;
	}
	Stream.feed(Chunk.GetData(), (size_t)Read);
	return true;
}

void FBlendityLinkClient::Tick()
{
	if (!Socket)
	{
		const double Now = FPlatformTime::Seconds();
		if (bAutoConnect && !bHeld && Now >= NextWatch)
		{
			NextWatch = Now + WatchSeconds;
			TryAutoConnect();
		}
		return;
	}
	if (bPending)
	{
		FinishConnect();
		return;
	}
	const double Start = FPlatformTime::Seconds();
	blendity_link::Message M;
	while (Socket && FPlatformTime::Seconds() - Start < BudgetSeconds)
	{
		if (Stream.next(M))
		{
			Applier->Apply(M);
			Messages++;
			continue;
		}
		if (!ReadSome())
			return;
	}
}
