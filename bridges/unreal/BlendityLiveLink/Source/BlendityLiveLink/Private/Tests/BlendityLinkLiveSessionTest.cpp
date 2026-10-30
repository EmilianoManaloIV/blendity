// SPDX-License-Identifier: GPL-2.0-or-later
// The Unreal bridge's live-session test (round 318): the plugin's own connection, ticked by the editor, follows a running
// Blendity, not a recording. Blendity's own test runs it (tests_round318 in tests/test_livelink.cpp, BLENDITY_TEST_BRIDGE
// naming the command):
//   UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Blendity.LiveLink.LiveSession; Quit" -unattended -nullrhi
// Once the editor's world has the Box it writes BLENDITY_BRIDGE_DUMP.ready: Blendity then moves, recolours and deforms the
// Box, turns a bone, deletes the Sphere, makes "Late" and deforms the dense sphere "Dense" ten times, the last to radius
// 1.2, and stops Live Link. When the discovery file goes, the world is checked and the report written to
// BLENDITY_BRIDGE_DUMP ("LIVE OK" or "LIVE FAILED"). Without BLENDITY_BRIDGE_DUMP it passes doing nothing.
#include "BlendityLinkProtocol.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
struct FLiveSession
{
	FString Dump;
	bool bReady = false;
	double Start = 0, Last = 0, Longest = 0;
	TArray<FString> Lines;
	int32 Failures = 0;

	void Check(bool bOk, const FString& What)
	{
		Lines.Add((bOk ? TEXT("ok: ") : TEXT("FAILED: ")) + What);
		Failures += bOk ? 0 : 1;
	}
};

UWorld* EditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

AActor* FindLabelled(const TCHAR* Label)
{
	UWorld* World = EditorWorld();
	if (!World)
		return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (It->GetActorLabel() == Label)
			return *It;
	return nullptr;
}

UStaticMesh* MeshOf(AActor* A)
{
	AStaticMeshActor* M = Cast<AStaticMeshActor>(A);
	return M ? M->GetStaticMeshComponent()->GetStaticMesh().Get() : nullptr;
}

void CheckWorld(FLiveSession& S)
{
	S.Check(FindLabelled(TEXT("Sphere")) == nullptr, TEXT("the Sphere deleted"));
	AActor* Box = FindLabelled(TEXT("Box"));
	S.Check(Box != nullptr, TEXT("the Box"));
	if (Box)
	{
		// Blendity (1, 2, 3) -> glTF (-1, 2, 3) -> Unreal (300, 100, 200) cm
		const FVector P = Box->GetActorLocation();
		S.Check(P.Equals(FVector(300, 100, 200), 1e-2), FString::Printf(TEXT("the Box moved to %s"), *P.ToString()));
		AStaticMeshActor* M = Cast<AStaticMeshActor>(Box);
		UMaterialInstanceDynamic* Mat = M ? Cast<UMaterialInstanceDynamic>(M->GetStaticMeshComponent()->GetMaterial(0)) : nullptr;
		FLinearColor C = FLinearColor::Black;
		if (Mat)
			Mat->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Color")), C);
		S.Check(FMath::Abs(C.R - 0.9f) < 1e-3f && FMath::Abs(C.G - 0.4f) < 1e-3f && FMath::Abs(C.B - 0.2f) < 1e-3f,
				FString::Printf(TEXT("the Box recolored: %s"), *C.ToString()));
	}
	AActor* Lower = FindLabelled(TEXT("Lower"));
	const double Angle = Lower && Lower->GetRootComponent() ? FMath::RadiansToDegrees(Lower->GetRootComponent()->GetRelativeRotation().Quaternion().GetAngle()) : 0.0;
	S.Check(FMath::Abs(Angle - 30.0) < 1e-2, FString::Printf(TEXT("the bone turned: %.3f degrees"), Angle));
	AActor* Late = FindLabelled(TEXT("Late"));
	S.Check(Late && Late->GetActorLocation().Equals(FVector(0, 0, 500), 1e-2),
			FString::Printf(TEXT("the object made live at %s"), Late ? *Late->GetActorLocation().ToString() : TEXT("nowhere")));
	UStaticMesh* Dense = MeshOf(FindLabelled(TEXT("Dense")));
	const double Radius = Dense ? Dense->GetBoundingBox().GetExtent().GetMax() : 0.0;  // (cm: 1.2 m)
	S.Check(FMath::Abs(Radius - 120.0) < 0.2, FString::Printf(TEXT("the dense mesh's last deform: radius %.2f cm"), Radius));
	AStaticMeshActor* DenseActor = Cast<AStaticMeshActor>(FindLabelled(TEXT("Dense")));
	// (the mesh built after the deforms keeps its CPU data for it: Unreal records that in its render data, not in
	// bAllowCPUAccess, so the collision setting is what is checked)
	const bool bCollision = DenseActor && DenseActor->GetStaticMeshComponent()->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics;
	S.Check(bCollision, FString::Printf(TEXT("the dense mesh has its collision back once the deforms stopped (collision %d)"),
									  DenseActor ? (int32)DenseActor->GetStaticMeshComponent()->GetCollisionEnabled() : -1));
}

/* A frame of the editor: the ready file once the Box is in, the longest frame once the dense mesh is, the checks when
 * the discovery file goes. True ends the test. */
bool Step(FLiveSession& S)
{
	const double Now = FPlatformTime::Seconds();
	if (MeshOf(FindLabelled(TEXT("Dense"))))
		S.Longest = FMath::Max(S.Longest, (Now - S.Last) * 1000.0);
	S.Last = Now;
	if (!S.bReady && FindLabelled(TEXT("Box")))
	{
		S.bReady = true;
		FFileHelper::SaveStringToFile(TEXT("Unreal ") + FEngineVersion::Current().ToString() + TEXT("\n"), *(S.Dump + TEXT(".ready")));
	}
	const bool bGone = S.bReady && !FPaths::FileExists(UTF8_TO_TCHAR(blendity_link::discovery_path().c_str()));
	if (!bGone && Now - S.Start < 600.0)
		return false;
	S.Check(bGone, TEXT("followed until Blendity stopped Live Link"));
	CheckWorld(S);
	S.Lines.Add(FString::Printf(TEXT("app: Unreal %s; the longest editor frame once the dense mesh was in: %.1f ms"), *FEngineVersion::Current().ToString(), S.Longest));
	S.Lines.Add(S.Failures == 0 ? FString(TEXT("LIVE OK")) : FString::Printf(TEXT("LIVE FAILED: %d problem(s)"), S.Failures));
	FFileHelper::SaveStringToFile(FString::Join(S.Lines, TEXT("\n")) + TEXT("\n"), *S.Dump);
	return true;
}
}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlendityLinkLiveSessionTest, "Blendity.LiveLink.LiveSession",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlendityLinkLiveSessionTest::RunTest(const FString&)
{
	const FString Dump = FPlatformMisc::GetEnvironmentVariable(TEXT("BLENDITY_BRIDGE_DUMP"));
	if (Dump.IsEmpty())
	{
		AddInfo(TEXT("No BLENDITY_BRIDGE_DUMP: Blendity's live test runs this (tests_round318)"));
		return true;
	}
	TSharedRef<FLiveSession> S = MakeShared<FLiveSession>();
	S->Dump = Dump;
	S->Start = S->Last = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S]() {
		if (!Step(*S))
			return false;
		for (const FString& L : S->Lines)
			if (L.StartsWith(TEXT("FAILED")))
				AddError(L);
		return true;
	}));
	return true;
}

#endif
