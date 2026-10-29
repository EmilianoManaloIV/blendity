// SPDX-License-Identifier: GPL-2.0-or-later
// The Unreal bridge's replay test (task 0277): the recorded Live Link session (Resources/livelink_stream.bin, or the
// repository's tests/fixtures one) through the protocol and the applier into a new editor world.
//   Session Frontend > Automation > Blendity.LiveLink.Replay, or
//   UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Blendity.LiveLink.Replay; Quit" -unattended -nullrhi
#include "BlendityLinkApplier.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlendityLinkReplayTest, "Blendity.LiveLink.Replay",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlendityLinkReplayTest::RunTest(const FString&)
{
	const FString Base = IPluginManager::Get().FindPlugin(TEXT("BlendityLiveLink"))->GetBaseDir();
	FString Path = FPaths::Combine(Base, TEXT("Resources"), TEXT("livelink_stream.bin"));
	if (!FPaths::FileExists(Path))
		Path = FPaths::Combine(Base, TEXT("../../.."), TEXT("tests"), TEXT("fixtures"), TEXT("livelink_stream.bin"));  // (bridges/unreal/BlendityLiveLink in the repository)
	TArray<uint8> Data;
	if (!TestTrue(TEXT("the recorded session read"), FFileHelper::LoadFileToArray(Data, *Path)))
		return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);  // (as an editor world has: deleting an actor asks for it)
	FBlendityLinkApplier Applier(World);
	blendity_link::Stream S;
	S.feed(Data.GetData(), Data.Num());
	blendity_link::Message M;
	int32 N = 0;
	while (S.next(M))
	{
		Applier.Apply(M);
		N++;
	}
	TestTrue(FString::Printf(TEXT("the stream framed whole (%d messages)"), N), N > 20 && S.pending() == 0);
	for (const TCHAR* Want : {TEXT("Main Camera"), TEXT("Directional Light"), TEXT("Plane"), TEXT("Box"), TEXT("Rig"), TEXT("Upper"), TEXT("Lower")})
		TestNotNull(FString::Printf(TEXT("the object %s"), Want), Applier.FindByName(Want));
	TestNull(TEXT("the deleted Sphere gone"), Applier.FindByName(TEXT("Sphere")));
	AStaticMeshActor* Box = Cast<AStaticMeshActor>(Applier.FindByName(TEXT("Box")));
	if (TestNotNull(TEXT("the Box"), Box))
	{
		// Blendity (1, 2, 3) -> glTF (-1, 2, 3) -> Unreal (300, 100, 200) cm
		TestTrue(FString::Printf(TEXT("the Box at %s"), *Box->GetActorLocation().ToString()), Box->GetActorLocation().Equals(FVector(300, 100, 200), 1e-3));
		TestNotNull(TEXT("the Box has a mesh"), Box->GetStaticMeshComponent()->GetStaticMesh().Get());
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
