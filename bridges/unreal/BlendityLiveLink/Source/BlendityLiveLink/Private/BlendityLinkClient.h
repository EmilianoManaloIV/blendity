// SPDX-License-Identifier: GPL-2.0-or-later
// The connection (task 0277): a non-blocking FSocket polled from the core ticker, at most 20 ms of applying a frame so
// the editor stays responsive. With Auto Connect on it reads Blendity's discovery file every 2 seconds while not
// connected, and after Blendity closes or restarts waits for it again; Disconnect holds it off until Connect.
#pragma once

#include "BlendityLinkApplier.h"
#include "BlendityLinkProtocol.h"
#include "CoreMinimal.h"

class FSocket;

class FBlendityLinkClient
{
public:
	~FBlendityLinkClient();
	void Tick();
	bool ConnectTo(int32 Port, const FString& Scene = FString());
	bool TryAutoConnect();
	void Disconnect(const FString& Why = TEXT("Not connected"));
	void Hold();

	bool IsConnected() const { return Socket != nullptr && !bPending; }
	bool HasSocket() const { return Socket != nullptr; }
	FString Status = TEXT("Not connected");
	int32 Messages = 0;
	bool bAutoConnect = true;
	int32 ManualPort = 0;  // (0: the discovery file's port)
	bool bHeld = false;
	const FBlendityLinkApplier* GetApplier() const { return Applier.Get(); }

private:
	void FinishConnect();
	bool ReadSome();

	FSocket* Socket = nullptr;
	bool bPending = false;
	double PendingSince = 0;
	double NextWatch = 0;
	int32 Port = 0;
	FString PendingScene;
	blendity_link::Stream Stream;
	TUniquePtr<FBlendityLinkApplier> Applier;
	TArray<uint8> Chunk;
};
