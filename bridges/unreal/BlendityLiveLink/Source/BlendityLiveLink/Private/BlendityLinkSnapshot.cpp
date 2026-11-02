// SPDX-License-Identifier: GPL-2.0-or-later
// Send to Blendity (round 374, task 0351): see BlendityLinkSnapshot.h.
#include "BlendityLinkSnapshot.h"

#include "BlendityLinkProtocol.h"
#include "AssetExportTask.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "Engine/Blueprint.h"
#include "Exporters/Exporter.h"
#include "Exporters/FbxExportOption.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
const FName LiveLinkTag(TEXT("BlendityLiveLink"));  // (the applier's: Blendity's own actors, never sent back)

std::string Utf8(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }

FString AttachPath(const AActor* A)
{
	FString Path = A->GetActorLabel();
	for (const AActor* P = A->GetAttachParentActor(); P; P = P->GetAttachParentActor())
		Path = P->GetActorLabel() + TEXT("/") + Path;
	return Path;
}

void ReadLight(const AActor* A, blendity_link::SnapActor& S)
{
	const ULightComponent* L = A->FindComponentByClass<ULightComponent>();
	if (!L)
		return;
	const FLinearColor C = L->GetLightColor();
	S.color[0] = C.R, S.color[1] = C.G, S.color[2] = C.B;
	S.intensity = L->Intensity;
	S.light = Cast<UDirectionalLightComponent>(L) ? 1 : Cast<USpotLightComponent>(L) ? 3 : Cast<UPointLightComponent>(L) ? 2 : Cast<URectLightComponent>(L) ? 4 : 0;
	const USpotLightComponent* Spot = Cast<USpotLightComponent>(L);
	if (Spot)
		S.outer_deg = Spot->OuterConeAngle, S.inner_deg = Spot->InnerConeAngle;
	if (const ULocalLightComponent* Local = Cast<ULocalLightComponent>(L))  // (lumens, EV or unitless: candela, as the snapshot's - review 374)
	{
		S.radius_cm = Local->AttenuationRadius;
		const float CosHalf = Spot ? FMath::Cos(FMath::DegreesToRadians(Spot->OuterConeAngle)) : -1.0f;
		S.intensity = L->Intensity * ULocalLightComponent::GetUnitsConversionFactor(Local->IntensityUnits, ELightUnits::Candelas, CosHalf);
	}
	if (const URectLightComponent* Rect = Cast<URectLightComponent>(L))
		S.width_cm = Rect->SourceWidth, S.height_cm = Rect->SourceHeight;
}

void ReadCamera(const AActor* A, blendity_link::SnapActor& S)
{
	const UCameraComponent* C = A->FindComponentByClass<UCameraComponent>();
	if (!C)
		return;
	S.camera = true;
	S.hfov_deg = C->FieldOfView, S.aspect = C->AspectRatio;
	S.ortho = C->ProjectionMode == ECameraProjectionMode::Orthographic, S.ortho_width_cm = C->OrthoWidth;
	S.near_cm = GNearClippingPlane;
}

/* Whether an actor goes: one with a mesh, a light or a camera, or a Blueprint's (not WorldSettings, brushes, fog...). */
bool Sendable(const AActor* A)
{
	return A->FindComponentByClass<UStaticMeshComponent>() || A->FindComponentByClass<USkeletalMeshComponent>() || A->FindComponentByClass<ULightComponent>() ||
	       A->FindComponentByClass<UCameraComponent>() || UBlueprint::GetBlueprintFromClass(A->GetClass());
}

blendity_link::SnapActor ReadActor(const AActor* A)
{
	blendity_link::SnapActor S;
	S.name = Utf8(A->GetActorLabel()), S.path = Utf8(AttachPath(A));
	if (UBlueprint::GetBlueprintFromClass(A->GetClass()))  // (a Blueprint's: its path, so the way back finds the same class)
		S.cls = Utf8(A->GetClass()->GetPathName());
	const USceneComponent* Placed = A->FindComponentByClass<ULightComponent>();  // (a light's or camera's own place in a Blueprint)
	Placed = Placed ? Placed : A->FindComponentByClass<UCameraComponent>();
	const FTransform T = Placed ? Placed->GetComponentTransform() : A->GetActorTransform();
	const FVector L = T.GetLocation(), Sc = T.GetScale3D();
	const FQuat Q = T.GetRotation();
	S.loc[0] = L.X, S.loc[1] = L.Y, S.loc[2] = L.Z;
	S.quat[0] = Q.X, S.quat[1] = Q.Y, S.quat[2] = Q.Z, S.quat[3] = Q.W;
	S.scale[0] = Sc.X, S.scale[1] = Sc.Y, S.scale[2] = Sc.Z;
	ReadLight(A, S);
	ReadCamera(A, S);
	return S;
}

/* The sent actors' model through Unreal's FBX exporter, as an automated task (no options dialog) with its options set
 * here, not the user's last ones (the axes model_rotation assumes); exactly the sent actors selected for it (Live
 * Link's own never in it: review 374), the user's selection put back after. */
bool ExportActors(UWorld* World, const FString& File, const TArray<AActor*>& Sent)
{
	TArray<AActor*> Was;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(Was);
	GEditor->SelectNone(false, true, false);
	for (AActor* A : Sent)
		GEditor->SelectActor(A, true, false, true);
	UFbxExportOption* Options = NewObject<UFbxExportOption>();
	Options->bASCII = false, Options->bForceFrontXAxis = false, Options->LevelOfDetail = false, Options->Collision = false;
	Options->VertexColor = true, Options->bExportMorphTargets = true;
	UAssetExportTask* Task = NewObject<UAssetExportTask>();
	Task->Object = World;
	Task->Filename = File;
	Task->bSelected = true;
	Task->bReplaceIdentical = true;
	Task->bPrompt = false;
	Task->bAutomated = true;
	Task->Options = Options;
	const bool bOk = UExporter::RunAssetExportTask(Task);
	GEditor->SelectNone(false, true, false);
	for (AActor* A : Was)
		GEditor->SelectActor(A, true, false, true);
	GEditor->NoteSelectionChange();
	return bOk;
}
}

bool BlendityWriteSnapshot(bool bSelectedOnly, FString& Message)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
		return Message = TEXT("Send to Blendity: no level is open"), false;
	blendity_link::Endpoint Found;
	const bool bLinked = blendity_link::read_discovery(blendity_link::discovery_path(), Found) && !Found.inbox.empty();
	const FString Inbox = bLinked ? FString(UTF8_TO_TCHAR(Found.inbox.c_str())) : FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Blendity"));
	const FString Name = World->GetMapName();
	const FString Folder = Inbox / (Name + TEXT(".blendity-snapshot")), Json = Folder / TEXT("snapshot.json");
	IFileManager::Get().MakeDirectory(*Folder, true);
	IFileManager::Get().Delete(*Json);  // (an earlier send's: never read half-written)
	std::vector<blendity_link::SnapActor> Actors;
	TArray<AActor*> Sent;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A->ActorHasTag(LiveLinkTag) || (bSelectedOnly && !A->IsSelected()) || A->IsEditorOnly() || !Sendable(A))
			continue;
		Actors.push_back(ReadActor(A));
		Sent.Add(A);
	}
	if (Actors.empty())
		return Message = bSelectedOnly ? TEXT("Send to Blendity: select the actors to send") : TEXT("Send to Blendity: the level has no meshes, lights or cameras"), false;
	if (!ExportActors(World, Folder / (Name + TEXT(".fbx")), Sent))
		return Message = TEXT("Send to Blendity: Unreal's FBX export failed (see the Output Log)"), false;
	const std::string Text = blendity_link::snapshot_json(Utf8(TEXT("Unreal ") + FEngineVersion::Current().ToString(EVersionComponent::Minor)), Utf8(Name),
	                                                      Utf8(Name + TEXT(".fbx")), Actors);
	const FString Tmp = Json + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(Text.c_str())), *Tmp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) || !IFileManager::Get().Move(*Json, *Tmp))
		return Message = TEXT("Send to Blendity: couldn't write ") + Json, false;
	Message = bLinked ? FString::Printf(TEXT("Sent to Blendity: %s (%d actors)"), *Name, (int)Actors.size())
	                  : FString::Printf(TEXT("Blendity's Live Link isn't running: the snapshot is in %s - drop it on Blendity"), *Folder);
	return true;
}
