// SPDX-License-Identifier: GPL-2.0-or-later
// Live Link's messages applied to the open level (task 0277): actors in a "Blendity Live Link" folder of the World
// Outliner, meshes as transient static meshes built from FMeshDescription (a deform rebuilds the positions), materials
// as dynamic instances of the engine's BasicShapeMaterial (its colour), lights and cameras as attached actors.
// Unreal's +X forward is Blendity's front, so a light or a camera needs no turn.
#pragma once

#include "BlendityLinkProtocol.h"
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class UWorld;
class UStaticMesh;
class UMaterialInstanceDynamic;
struct FMeshDescription;

class FBlendityLinkApplier
{
public:
	explicit FBlendityLinkApplier(UWorld* InWorld);
	void Apply(const blendity_link::Message& M);

	int32 ObjectCount() const { return Objects.Num(); }
	bool IsPlaying() const { return bPlaying; }
	AActor* FindByName(const FString& Name) const;
	const TMap<int64, TWeakObjectPtr<AActor>>& GetObjects() const { return Objects; }
	/* Each tick (round 318): a mesh whose deforms have stopped for half a second is built once more with its CPU data
	 * and gets its collision back - while it deforms it has neither, as cooking it each time cost 259 ms a frame on a
	 * 32,000-vertex mesh. */
	void Settle();

private:
	void OnObject(const blendity_link::Json& J);
	void OnTransform(const blendity_link::Json& J);
	void OnPose(const blendity_link::Json& J);
	void OnMesh(const blendity_link::Json& J, const TArray<uint8>& Bin);
	void OnDeform(const blendity_link::Json& J, const TArray<uint8>& Bin);
	void OnMaterial(const blendity_link::Json& J);
	void OnLight(const blendity_link::Json& J);
	void OnCamera(const blendity_link::Json& J);
	void Remove(int64 Id);
	void EndSync();
	void Reparent(int64 Id);
	void AssignMaterials(int64 Id);
	AActor* Alive(int64 Id) const;
	AActor* Spawn(int64 Id, const FString& Kind);
	static void FillDescription(const blendity_link::GlbMesh& G, FMeshDescription& Desc);
	static bool DeformDescription(const blendity_link::GlbMesh& G, FMeshDescription& Desc);
	UStaticMesh* BuildMesh(const FMeshDescription& Desc, int32 Sections, const FString& Name, bool bCollision = true) const;
	static void SetTrs(AActor* A, const blendity_link::Json& P, const blendity_link::Json& Q, const blendity_link::Json& S);

	TWeakObjectPtr<UWorld> World;
	TMap<int64, TWeakObjectPtr<AActor>> Objects;
	TMap<int64, FString> Kinds;
	TMap<int64, int64> Parents;
	TMap<int64, TArray<int32>> Slots;
	TMap<int64, blendity_link::GlbMesh> Meshes;
	TMap<int64, FString> Topology;
	TMap<int64, double> Deforming;  // (the time of each deforming mesh's last deform: Settle)
	TMap<int64, TSharedPtr<FMeshDescription>> Descriptions;  // (round 318: a deform changes the positions in place)
	TMap<int32, TStrongObjectPtr<UMaterialInstanceDynamic>> Materials;
	TMap<int64, TWeakObjectPtr<AActor>> Parts;  // (a light's or a camera's own actor, attached to its object)
	TSet<int64>* Seen = nullptr;
	TSet<int64> SeenStorage;
	bool bPlaying = false;
};
