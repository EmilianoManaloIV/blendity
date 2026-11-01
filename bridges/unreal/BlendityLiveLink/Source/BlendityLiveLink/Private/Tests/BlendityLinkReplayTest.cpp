// SPDX-License-Identifier: GPL-2.0-or-later
// The Unreal bridge's replay test (task 0277): the recorded Live Link session (Resources/livelink_stream.bin, or the
// repository's tests/fixtures one) through the protocol and the applier into a new editor world.
//   Session Frontend > Automation > Blendity.LiveLink.Replay, or
//   UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Blendity.LiveLink.Replay; Quit" -unattended -nullrhi
#include "BlendityLinkApplier.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
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
	Applier.Flush();  // (round 380: what LOD and collider messages changed, once - as the client does each tick)
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
		/* Round 377: the Base Map from its PNG bytes (a 4x4 checker) on the Box's material. */
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Box->GetStaticMeshComponent()->GetMaterial(0));
		UTexture* BaseMap = nullptr;
		TestTrue(TEXT("the Base Map came from its bytes"), Mid && Mid->GetTextureParameterValue(FName(TEXT("BaseMap")), BaseMap) && BaseMap && Cast<UTexture2D>(BaseMap) &&
		                                                  Cast<UTexture2D>(BaseMap)->GetSizeX() == 4);
	}
	/* Round 380: the crate's three LODs and its box collider, 1 x 2 x 1 m (Unreal: 100 x 100 x 200 cm), centred -0.1 m in
	 * glTF's X (Unreal's Y: +10 cm, as -100x). */
	AStaticMeshActor* Crate = Cast<AStaticMeshActor>(Applier.FindByName(TEXT("Crate")));
	if (TestNotNull(TEXT("the Crate"), Crate))
	{
		UStaticMesh* Mesh = Crate->GetStaticMeshComponent()->GetStaticMesh().Get();
		TestTrue(FString::Printf(TEXT("the Crate's LODs: %d"), Mesh ? Mesh->GetNumLODs() : 0), Mesh && Mesh->GetNumLODs() == 3);
		if (Mesh && Mesh->GetRenderData() && Mesh->GetNumLODs() == 3)
			TestTrue(TEXT("LOD 1 from half the screen"), FMath::IsNearlyEqual(Mesh->GetRenderData()->ScreenSize[1].Default, 0.5f, 1e-5f));
		UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
		const FKBoxElem* Box2 = Body && Body->AggGeom.BoxElems.Num() == 1 ? &Body->AggGeom.BoxElems[0] : nullptr;
		TestTrue(TEXT("the Crate's box collider"), Box2 && FMath::IsNearlyEqual(Box2->Z, 200.0, 1e-3) && FMath::IsNearlyEqual(Box2->X, 100.0, 1e-3) &&
		                                             Box2->Center.Equals(FVector(0, 10, 0), 1e-3));
	}
	/* Round 376: the look - fog from 5 m, half a stop up, Bloom 0.8 and a Vignette 0.3. */
	if (AExponentialHeightFog* Fg = Cast<AExponentialHeightFog>(Applier.GetFog()))
		TestTrue(TEXT("the fog starts at 5 m"), FMath::IsNearlyEqual(Fg->GetComponent()->StartDistance, 500.0f, 1e-2f) && Fg->GetComponent()->IsVisible());
	else
		AddError(TEXT("no fog"));
	if (APostProcessVolume* V = Cast<APostProcessVolume>(Applier.GetPost()))
		TestTrue(TEXT("the exposure, Bloom and Vignette"), V->bUnbound && FMath::IsNearlyEqual(V->Settings.AutoExposureBias, 0.5f, 1e-4f) &&
		                                                    FMath::IsNearlyEqual(V->Settings.BloomIntensity, 0.8f, 1e-4f) && FMath::IsNearlyEqual(V->Settings.VignetteIntensity, 0.3f, 1e-4f));
	else
		AddError(TEXT("no post-process volume"));
	TestNotNull(TEXT("the sky light"), Applier.GetSky());
	{  // (a reconnect: a new applier takes the look's actors back instead of making more - review 376)
		FBlendityLinkApplier Again(World);
		TestTrue(TEXT("the look's actors adopted on reconnect"), Again.GetSky() == Applier.GetSky() && Again.GetFog() == Applier.GetFog() && Again.GetPost() == Applier.GetPost());
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
