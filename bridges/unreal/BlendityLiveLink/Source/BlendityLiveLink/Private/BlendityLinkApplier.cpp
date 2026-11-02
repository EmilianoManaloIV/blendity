// SPDX-License-Identifier: GPL-2.0-or-later
// Live Link's messages applied to the open level (task 0277). See BlendityLinkApplier.h.
#include "BlendityLinkApplier.h"
#include "PhysicsEngine/BodySetup.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Modules/ModuleManager.h"
#include "MeshDescription.h"
#include "StaticMeshResources.h"
#include "StaticMeshAttributes.h"

namespace
{
const FName FolderName(TEXT("Blendity Live Link"));
const FName IdTag(TEXT("BlendityLiveLink"));
}

// (FVector from the protocol's Vec3)
static FVector Vec(const blendity_link::Vec3& V) { return FVector(V.x, V.y, V.z); }

FBlendityLinkApplier::FBlendityLinkApplier(UWorld* InWorld) : World(InWorld)
{
	if (!InWorld)
		return;
	for (TActorIterator<AActor> It(InWorld); It; ++It)  // (actors of an earlier session: found again by their ids)
	{
		AdoptLook(*It);
		for (const FName& Tag : It->Tags)
		{
			const FString S = Tag.ToString();
			if (!S.StartsWith(TEXT("BlendityId:")))
				continue;
			const int64 Id = FCString::Atoi64(*S.RightChop(11));
			Objects.Add(Id, *It);
			for (const FName& K : It->Tags)  // (its kind too: an adopted actor isn't respawned by the next object message)
				if (K.ToString().StartsWith(TEXT("BlendityKind:")))
					Kinds.Add(Id, K.ToString().RightChop(13));
		}
	}
}

AActor* FBlendityLinkApplier::Alive(int64 Id) const
{
	const TWeakObjectPtr<AActor>* A = Objects.Find(Id);
	return A && A->IsValid() ? A->Get() : nullptr;
}

AActor* FBlendityLinkApplier::FindByName(const FString& Name) const
{
	for (const auto& Kv : Objects)
		if (Kv.Value.IsValid() && Kv.Value->GetActorLabel() == Name)
			return Kv.Value.Get();
	return nullptr;
}

void FBlendityLinkApplier::Apply(const blendity_link::Message& M)
{
	const blendity_link::Json& J = M.json;
	const FString T(UTF8_TO_TCHAR(M.type.c_str()));
	TArray<uint8> Bin(M.bin.data(), M.bin.size());
	if (T == TEXT("sync")) { SeenStorage.Reset(); Seen = &SeenStorage; bPlaying = J["playing"].boolean(); }
	else if (T == TEXT("sync_end")) EndSync();
	else if (T == TEXT("play")) bPlaying = J["playing"].boolean();
	else if (T == TEXT("object")) OnObject(J);
	else if (T == TEXT("transform")) OnTransform(J);
	else if (T == TEXT("pose")) OnPose(J);
	else if (T == TEXT("delete")) Remove(J["id"].id());
	else if (T == TEXT("mesh")) OnMesh(J, Bin);
	else if (T == TEXT("deform")) OnDeform(J, Bin);
	else if (T == TEXT("material")) OnMaterial(J);
	else if (T == TEXT("texture")) OnTexture(J, Bin);
	else if (T == TEXT("light")) OnLight(J);
	else if (T == TEXT("camera")) OnCamera(J);
	else if (T == TEXT("world")) OnWorld(J);
	else if (T == TEXT("render")) OnRender(J);
	else if (T == TEXT("filters")) OnFilters(J);
	else if (T == TEXT("lod")) OnLod(J);  // (round 380)
	else if (T == TEXT("lodmesh")) OnLodMesh(J, Bin);
	else if (T == TEXT("collider")) OnCollider(J);
}

void FBlendityLinkApplier::EndSync()
{
	if (!Seen)
		return;
	TArray<int64> Gone;
	for (const auto& Kv : Objects)
		if (!Seen->Contains(Kv.Key))
			Gone.Add(Kv.Key);
	for (int64 Id : Gone)
		Remove(Id);
	Seen = nullptr;
}

void FBlendityLinkApplier::SetTrs(AActor* A, const blendity_link::Json& P, const blendity_link::Json& Q, const blendity_link::Json& S)
{
	const blendity_link::Quat R = blendity_link::rotation_to_unreal(Q);
	const FTransform Xf(FQuat(R.x, R.y, R.z, R.w).GetNormalized(), Vec(blendity_link::position_to_unreal(P)), Vec(blendity_link::scale_to_unreal(S)));
	A->SetActorRelativeTransform(Xf);
}

AActor* FBlendityLinkApplier::Spawn(int64 Id, const FString& Kind)
{
	UWorld* W = World.Get();
	if (!W)
		return nullptr;
	FActorSpawnParameters P;
	P.ObjectFlags |= RF_Transient;  // (the level isn't dirtied: Blendity owns these)
	AActor* A = nullptr;
	if (Kind == TEXT("mesh"))
		A = W->SpawnActor<AStaticMeshActor>(P);
	else
	{
		A = W->SpawnActor<AActor>(P);
		if (!A)
			return nullptr;
		USceneComponent* Root = NewObject<USceneComponent>(A, TEXT("Root"));
		A->SetRootComponent(Root);
		Root->RegisterComponent();
	}
	if (!A)
		return nullptr;
	A->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	A->Tags.Add(IdTag);
	A->Tags.Add(FName(*FString::Printf(TEXT("BlendityId:%lld"), Id)));
	A->Tags.Add(FName(*(TEXT("BlendityKind:") + Kind)));
	A->SetFolderPath(FolderName);
	return A;
}

void FBlendityLinkApplier::OnObject(const blendity_link::Json& J)
{
	const int64 Id = J["id"].id();
	if (Seen)
		Seen->Add(Id);
	const FString Kind(UTF8_TO_TCHAR(J["kind"].string().c_str()));
	AActor* A = Alive(Id);
	const FString* Was = Kinds.Find(Id);
	if (A && (!Was || *Was != Kind))
	{
		Remove(Id);
		A = nullptr;
	}
	if (!A)
	{
		A = Spawn(Id, Kind);
		if (!A)
			return;
		Objects.Add(Id, A);
		Kinds.Add(Id, Kind);
	}
	A->SetActorLabel(UTF8_TO_TCHAR(J["name"].string().c_str()));
	A->SetActorHiddenInGame(!J["active"].boolean(true));
	A->SetIsTemporarilyHiddenInEditor(!J["active"].boolean(true));
	Parents.Add(Id, J["parent"].id());
	Reparent(Id);
	SetTrs(A, J["position"], J["rotation"], J["scale"]);
	for (const auto& Kv : Parents)
		if (Kv.Value == Id)
			Reparent(Kv.Key);  // (children that came before their parent)
	if (!J.has("materials"))
		return;
	TArray<int32> Keys;
	for (const blendity_link::Json& K : J["materials"].arr)
		Keys.Add((int32)K.number(0));
	Slots.Add(Id, Keys);
	AssignMaterials(Id);
}

void FBlendityLinkApplier::Reparent(int64 Id)
{
	AActor* A = Alive(Id);
	if (!A)
		return;
	const int64* Pid = Parents.Find(Id);
	if (!Pid || *Pid == 0)
	{
		if (A->GetAttachParentActor())
			A->DetachFromActor(FDetachmentTransformRules::KeepRelativeTransform);
		return;
	}
	AActor* P = Alive(*Pid);
	if (P && A->GetAttachParentActor() != P)
		A->AttachToActor(P, FAttachmentTransformRules::KeepRelativeTransform);
}

void FBlendityLinkApplier::OnTransform(const blendity_link::Json& J)
{
	if (AActor* A = Alive(J["id"].id()))
		SetTrs(A, J["position"], J["rotation"], J["scale"]);
}

void FBlendityLinkApplier::OnPose(const blendity_link::Json& J)
{
	for (const blendity_link::Json& B : J["bones"].arr)
	{
		AActor* A = B.size() >= 11 ? Alive(B[0].id()) : nullptr;
		if (!A)
			continue;
		blendity_link::Json P, Q, S;
		P.type = Q.type = S.type = blendity_link::Json::Arr;
		P.arr = {B[1], B[2], B[3]};
		Q.arr = {B[4], B[5], B[6], B[7]};
		S.arr = {B[8], B[9], B[10]};
		SetTrs(A, P, Q, S);
	}
}

void FBlendityLinkApplier::Remove(int64 Id)
{
	if (AActor* A = Alive(Id))
	{
		TArray<AActor*> Kids;
		A->GetAttachedActors(Kids);
		for (AActor* K : Kids)
			K->DetachFromActor(FDetachmentTransformRules::KeepRelativeTransform);  // (children wait at the root for their own delete)
		if (const TWeakObjectPtr<AActor>* Part = Parts.Find(Id))
			if (Part->IsValid())
				Part->Get()->Destroy();
		A->Destroy();
	}
	Objects.Remove(Id);
	Kinds.Remove(Id);
	Parents.Remove(Id);
	Slots.Remove(Id);
	Meshes.Remove(Id);
	Topology.Remove(Id);
	Descriptions.Remove(Id);
	Deforming.Remove(Id);
	LodMsgs.Remove(Id);
	LodDescs.Remove(Id);
	ColliderMsgs.Remove(Id);
	Parts.Remove(Id);
}

/* A mesh's description from its glTF: every vertex its own instance (the glTF's split vertices), a polygon group a section.
 * Indexing is suspended while it fills - creating each triangle's edges one by one took 88 ms for 46,000 vertices. */
void FBlendityLinkApplier::FillDescription(const blendity_link::GlbMesh& G, FMeshDescription& Desc)
{
	FStaticMeshAttributes Attr(Desc);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> Uvs = Attr.GetVertexInstanceUVs();
	size_t Triangles = 0;
	for (const std::vector<int>& Tris : G.sections)
		Triangles += Tris.size() / 3;
	Desc.SuspendVertexInstanceIndexing();
	Desc.SuspendEdgeIndexing();
	Desc.SuspendPolygonIndexing();
	Desc.SuspendPolygonGroupIndexing();
	Desc.SuspendUVIndexing();
	Desc.ReserveNewVertices((int32)G.positions.size());
	Desc.ReserveNewVertexInstances((int32)G.positions.size());
	Desc.ReserveNewTriangles((int32)Triangles);
	Desc.ReserveNewPolygons((int32)Triangles);
	Desc.ReserveNewEdges((int32)Triangles * 3);
	TArray<FVertexInstanceID> Instances;
	Instances.Reserve((int32)G.positions.size());
	for (size_t I = 0; I < G.positions.size(); I++)
	{
		const FVertexID V = Desc.CreateVertex();
		Positions[V] = FVector3f(Vec(G.positions[I]));
		const FVertexInstanceID VI = Desc.CreateVertexInstance(V);
		Normals[VI] = FVector3f(Vec(G.normals[I]));
		Uvs[VI] = FVector2f(G.uvs[I * 2], G.uvs[I * 2 + 1]);
		Instances.Add(VI);
	}
	for (const std::vector<int>& Tris : G.sections)
	{
		const FPolygonGroupID Group = Desc.CreatePolygonGroup();
		for (size_t T = 0; T + 2 < Tris.size(); T += 3)
			Desc.CreateTriangle(Group, {Instances[Tris[T]], Instances[Tris[T + 1]], Instances[Tris[T + 2]]});
	}
	Desc.ResumeVertexInstanceIndexing();
	Desc.ResumeEdgeIndexing();
	Desc.ResumePolygonIndexing();
	Desc.ResumePolygonGroupIndexing();
	Desc.ResumeUVIndexing();
}

/* A deform into the kept description: the vertices are in the glTF's order (FillDescription made them so), only their
 * positions and normals change. False when the counts differ (a newer mesh is on its way). */
bool FBlendityLinkApplier::DeformDescription(const blendity_link::GlbMesh& G, FMeshDescription& Desc)
{
	FStaticMeshAttributes Attr(Desc);
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	if (Desc.Vertices().Num() != (int32)G.positions.size() || Desc.VertexInstances().Num() != (int32)G.positions.size())
		return false;
	for (size_t I = 0; I < G.positions.size(); I++)
	{
		Positions[FVertexID((int32)I)] = FVector3f(Vec(G.positions[I]));
		Normals[FVertexInstanceID((int32)I)] = FVector3f(Vec(G.normals[I]));
	}
	return true;
}

UStaticMesh* FBlendityLinkApplier::BuildMesh(const FMeshDescription& Desc, int32 Sections, const FString& Name, bool bCollision) const
{
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UStaticMesh::StaticClass(), FName(*Name)), RF_Transient);
	for (int32 S = 0; S < Sections; S++)
		Mesh->GetStaticMaterials().Add(FStaticMaterial());
	if (!bCollision)
	{
		Mesh->CreateBodySetup();  // (a deforming mesh: no triangles for collision to read, so none asked for)
		Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	}
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bAllowCpuAccess = bCollision;  // (collision reads the triangles on the CPU: traces and clicks in Play)
	Params.bMarkPackageDirty = false;  // (transient: nothing to save)
	Mesh->BuildFromMeshDescriptions({&Desc}, Params);
	return Mesh;
}

void FBlendityLinkApplier::OnMesh(const blendity_link::Json& J, const TArray<uint8>& Bin)
{
	const int64 Id = J["id"].id();
	AStaticMeshActor* A = Cast<AStaticMeshActor>(Alive(Id));
	if (!A)
		return;  // (a mesh for an object already gone)
	blendity_link::GlbMesh G;
	if (!blendity_link::read_glb(Bin.GetData(), Bin.Num(), G))
		return;
	TSharedRef<FMeshDescription> Desc = MakeShared<FMeshDescription>();
	FillDescription(G, *Desc);
	Descriptions.Add(Id, Desc);
	Deforming.Remove(Id);
	Meshes.Add(Id, MoveTemp(G));
	Topology.Add(Id, FString::Printf(TEXT("%.17g/%.17g"), J["topology"].number(0), J["verts"].number(0)));
	LodDescs.Remove(Id);  // (review 380: levels of the old mesh - its slots may differ; the new ones follow it)
	Rebuild(Id);
}

/* Round 380: the actor's static mesh made from its description and, with an LOD Group whose levels have all come, those
 * levels as its LODs; its collider as the body's simple collision. */
void FBlendityLinkApplier::Rebuild(int64 Id)
{
	AStaticMeshActor* A = Cast<AStaticMeshActor>(Alive(Id));
	const TSharedPtr<FMeshDescription>* Desc = Descriptions.Find(Id);
	const blendity_link::GlbMesh* G = Meshes.Find(Id);
	if (!A || !Desc || !G)
		return;
	TArray<const FMeshDescription*> Levels{Desc->Get()};
	const blendity_link::Json* Lod = LodMsgs.Find(Id);
	const TMap<int32, TSharedPtr<FMeshDescription>>* More = LodDescs.Find(Id);
	const int32 N = Lod ? (int32)(*Lod)["screen"].size() : 0;
	for (int32 L = 1; L < N && More && More->Contains(L); L++)
		Levels.Add((*More)[L].Get());
	if (Levels.Num() < N)
		Levels.SetNum(1);  // (a level still on its way: the mesh alone until it comes)
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UStaticMesh::StaticClass(), FName(*A->GetActorLabel())), RF_Transient);
	for (int32 S = 0; S < (int32)G->sections.size(); S++)
		Mesh->GetStaticMaterials().Add(FStaticMaterial());
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bAllowCpuAccess = true;  // (collision reads the triangles on the CPU: traces and clicks in Play)
	Params.bMarkPackageDirty = false;
	Mesh->BuildFromMeshDescriptions(Levels, Params);
	for (int32 L = 1; L < Levels.Num() && Mesh->GetRenderData(); L++)  // (LOD L from where the object stands Blendity's screen[L-1])
		Mesh->GetRenderData()->ScreenSize[L].Default = (float)(*Lod)["screen"][L - 1].number(0);
	ApplyCollision(Id, Mesh);
	DirtyMesh.Remove(Id);
	DirtyCollision.Remove(Id);
	A->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	A->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	AssignMaterials(Id);
}

/* The collider as the body's simple collision: a box, sphere or capsule (Unreal's sphyl, up Z as Blendity's is up Y) at
 * its centre, or the mesh's convex hull; a Mesh collider is the triangles themselves (complex as simple). */
void FBlendityLinkApplier::ApplyCollision(int64 Id, UStaticMesh* Mesh) const
{
	const blendity_link::Json* C = ColliderMsgs.Find(Id);
	if (!C)
		return;
	const FString Shape = FString(UTF8_TO_TCHAR((*C)["shape"].string().c_str()));
	Mesh->CreateBodySetup();
	UBodySetup* Body = Mesh->GetBodySetup();
	Body->AggGeom.EmptyElements();
	Body->CollisionTraceFlag = CTF_UseDefault;
	if (Shape == TEXT("none"))
	{
		Body->InvalidatePhysicsData();
		Body->CreatePhysicsMeshes();
		return;
	}
	const FVector Center = Vec(blendity_link::position_to_unreal((*C)["center"]));
	const FVector Size = Vec(blendity_link::scale_to_unreal((*C)["size"])) * 100.0;
	const double Radius = (*C)["radius"].number(0.5) * 100.0, Height = (*C)["height"].number(2.0) * 100.0;
	if (Shape == TEXT("box"))
	{
		FKBoxElem Box((float)Size.X, (float)Size.Y, (float)Size.Z);
		Box.Center = Center;
		Body->AggGeom.BoxElems.Add(Box);
	}
	else if (Shape == TEXT("sphere"))
	{
		FKSphereElem Sphere((float)Radius);
		Sphere.Center = Center;
		Body->AggGeom.SphereElems.Add(Sphere);
	}
	else if (Shape == TEXT("capsule"))
	{
		FKSphylElem Capsule((float)Radius, (float)FMath::Max(0.0, Height - 2.0 * Radius));
		Capsule.Center = Center;
		Body->AggGeom.SphylElems.Add(Capsule);
	}
	else if (Shape == TEXT("convex"))
	{
		FKConvexElem Hull;
		for (const blendity_link::Vec3& P : Meshes.FindChecked(Id).positions)
			Hull.VertexData.Add(Vec(P));
		Hull.UpdateElemBox();
		Body->AggGeom.ConvexElems.Add(Hull);
	}
	Body->CollisionTraceFlag = Shape == TEXT("mesh") ? CTF_UseComplexAsSimple : CTF_UseDefault;
	Body->InvalidatePhysicsData();
	Body->CreatePhysicsMeshes();
}

void FBlendityLinkApplier::OnLod(const blendity_link::Json& J)
{
	const int64 Id = J["id"].id();
	LodMsgs.Add(Id, J);
	if (TMap<int32, TSharedPtr<FMeshDescription>>* More = LodDescs.Find(Id))
		for (auto It = More->CreateIterator(); It; ++It)
			if (It.Key() >= (int32)J["screen"].size())
				It.RemoveCurrent();
	DirtyMesh.Add(Id);
}

/* Review 380: the actors LOD and collider messages changed, once for the batch - a full rebuild for their levels, the
 * body alone for a collider. */
void FBlendityLinkApplier::Flush()
{
	for (int64 Id : DirtyMesh.Array())
		if (!Deforming.Contains(Id))  // (a mesh still deforming is rebuilt whole when it settles)
			Rebuild(Id);
	for (int64 Id : DirtyCollision.Array())
		if (!Deforming.Contains(Id))
			UpdateCollision(Id);
	for (int64 Id : DirtyCollision.Array())
		if (!Deforming.Contains(Id))
			DirtyCollision.Remove(Id);
}

void FBlendityLinkApplier::UpdateCollision(int64 Id)
{
	AStaticMeshActor* A = Cast<AStaticMeshActor>(Alive(Id));
	UStaticMesh* Mesh = A ? A->GetStaticMeshComponent()->GetStaticMesh().Get() : nullptr;
	if (!Mesh || !Meshes.Contains(Id))
		return;
	ApplyCollision(Id, Mesh);
	A->GetStaticMeshComponent()->RecreatePhysicsState();
	DirtyCollision.Remove(Id);
}

void FBlendityLinkApplier::OnLodMesh(const blendity_link::Json& J, const TArray<uint8>& Bin)
{
	const int64 Id = J["id"].id();
	const int32 Level = (int32)J["level"].number(0);
	blendity_link::GlbMesh G;
	const blendity_link::Json* Lod = LodMsgs.Find(Id);
	if (Level < 1 || !Lod || Level >= (int32)(*Lod)["screen"].size())
		return;  // (a level the group no longer has: built before it shrank or went - review 380)
	if (!Alive(Id) || !blendity_link::read_glb(Bin.GetData(), Bin.Num(), G))
		return;
	TSharedPtr<FMeshDescription> Desc = MakeShared<FMeshDescription>();
	FillDescription(G, *Desc);
	LodDescs.FindOrAdd(Id).Add(Level, Desc);
	DirtyMesh.Add(Id);
}

void FBlendityLinkApplier::OnCollider(const blendity_link::Json& J)
{
	const int64 Id = J["id"].id();
	ColliderMsgs.Add(Id, J);
	DirtyCollision.Add(Id);
}

void FBlendityLinkApplier::OnDeform(const blendity_link::Json& J, const TArray<uint8>& Bin)
{
	const int64 Id = J["id"].id();
	AStaticMeshActor* A = Cast<AStaticMeshActor>(Alive(Id));
	blendity_link::GlbMesh* G = Meshes.Find(Id);
	const FString* Topo = Topology.Find(Id);
	if (!A || !G || !Topo)
		return;
	if (*Topo != FString::Printf(TEXT("%.17g/%.17g"), J["topology"].number(0), J["verts"].number(0)))
		return;  // (a newer mesh is on its way; a deform follows it)
	if (!blendity_link::apply_deform(*G, Bin.GetData(), Bin.Num()))
		return;
	TSharedPtr<FMeshDescription>* Desc = Descriptions.Find(Id);
	if (!Desc || !DeformDescription(*G, **Desc))
		return;
	A->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);  // (first: setting the mesh would ask for its collision)
	A->GetStaticMeshComponent()->SetStaticMesh(BuildMesh(**Desc, (int32)G->sections.size(), A->GetActorLabel(), false));  // (no CPU copy, no cooking: Settle)
	Deforming.Add(Id, FPlatformTime::Seconds());
	AssignMaterials(Id);
}

void FBlendityLinkApplier::Settle()
{
	const double Now = FPlatformTime::Seconds();
	for (auto It = Deforming.CreateIterator(); It; ++It)
	{
		if (Now - It.Value() < 0.5)
			continue;
		const int64 Id = It.Key();
		It.RemoveCurrent();
		AStaticMeshActor* A = Cast<AStaticMeshActor>(Alive(Id));
		const blendity_link::GlbMesh* G = Meshes.Find(Id);
		const TSharedPtr<FMeshDescription>* Desc = Descriptions.Find(Id);
		if (!A || !G || !Desc)
			continue;
		Rebuild(Id);  // (round 380: its LODs and collider with it)
	}
	Flush();  // (round 380)
}

void FBlendityLinkApplier::AssignMaterials(int64 Id)
{
	AStaticMeshActor* A = Cast<AStaticMeshActor>(Alive(Id));
	const TArray<int32>* Keys = Slots.Find(Id);
	if (!A || !Keys || Keys->Num() == 0)
		return;
	const blendity_link::GlbMesh* G = Meshes.Find(Id);
	const int32 Count = G ? (int32)G->sections.size() : Keys->Num();
	for (int32 I = 0; I < Count; I++)
	{
		const int32 Slot = G ? G->slots[I] : I;
		const int32 Key = (*Keys)[FMath::Clamp(Slot, 0, Keys->Num() - 1)];
		const TStrongObjectPtr<UMaterialInstanceDynamic>* M = Materials.Find(Key);
		if (M && M->IsValid())
			A->GetStaticMeshComponent()->SetMaterial(I, M->Get());
	}
}

/* Round 377: the base of Live Link's materials - a Color parameter times a BaseMap texture parameter (white until a
 * texture comes) as the base colour - made once in code (the engine's BasicShapeMaterial has no texture). */
static TStrongObjectPtr<UMaterial> GTexturedBase;  // (released at the module's shutdown, before UObjects go: review 377)

void BlendityReleaseTexturedBase() { GTexturedBase.Reset(); }

static UMaterialInterface* TexturedBase()
{
	TStrongObjectPtr<UMaterial>& Made = GTexturedBase;
	if (Made.IsValid())
		return Made.Get();
	UMaterial* M = NewObject<UMaterial>(GetTransientPackage(), TEXT("BlendityLiveLinkMaterial"), RF_Transient);
	UMaterialExpressionVectorParameter* Col = NewObject<UMaterialExpressionVectorParameter>(M);
	Col->ParameterName = TEXT("Color");
	Col->DefaultValue = FLinearColor::White;
	UMaterialExpressionTextureSampleParameter2D* Tex = NewObject<UMaterialExpressionTextureSampleParameter2D>(M);
	Tex->ParameterName = TEXT("BaseMap");
	Tex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	UMaterialExpressionMultiply* Mul = NewObject<UMaterialExpressionMultiply>(M);
	Mul->A.Expression = Col;
	Mul->B.Expression = Tex;
	M->GetExpressionCollection().AddExpression(Col);
	M->GetExpressionCollection().AddExpression(Tex);
	M->GetExpressionCollection().AddExpression(Mul);
	M->GetEditorOnlyData()->BaseColor.Expression = Mul;
	M->PreEditChange(nullptr);
	M->PostEditChange();
	Made.Reset(M);
	return M;
}

/* Does a material message name this texture key in one of its maps? */
static bool NamesTexture(const blendity_link::Json& J, int32 Key)
{
	for (const auto& Kv : J["maps"].obj)
		if ((int32)Kv.second["texture"].number(0) == Key)
			return true;
	return false;
}

/* Round 377: a texture's pixels as PNG bytes, decoded into a transient texture of its own; the materials naming its
 * key take it. */
void FBlendityLinkApplier::OnTexture(const blendity_link::Json& J, const TArray<uint8>& Bin)
{
	const int32 Key = (int32)J["key"].number(0);
	IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Png = Wrappers.CreateImageWrapper(EImageFormat::PNG);
	TArray64<uint8> Raw;
	if (!Png.IsValid() || !Png->SetCompressed(Bin.GetData(), Bin.Num()) || !Png->GetRaw(ERGBFormat::BGRA, 8, Raw))
		return;
	/* The key's texture again when its size is the same, its pixels written in place (review 377: a new one each message
	 * held gigabytes until a garbage collection while a texture was dragged). */
	const TStrongObjectPtr<UTexture2D>* Had = Textures.Find(Key);
	UTexture2D* Tex = Had && Had->IsValid() && (*Had)->GetSizeX() == Png->GetWidth() && (*Had)->GetSizeY() == Png->GetHeight() ? Had->Get() : nullptr;
	if (!Tex)
		Tex = UTexture2D::CreateTransient(Png->GetWidth(), Png->GetHeight(), PF_B8G8R8A8);
	if (!Tex)
		return;
	Tex->SRGB = !J["non_color"].boolean();
	void* Data = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Raw.GetData(), FMath::Min<int64>(Raw.Num(), Tex->GetPlatformData()->Mips[0].BulkData.GetBulkDataSize()));
	Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
	Tex->UpdateResource();
	Textures.Add(Key, TStrongObjectPtr<UTexture2D>(Tex));
	TArray<blendity_link::Json> Again;
	for (const auto& Kv : MaterialMsgs)
		if (NamesTexture(Kv.Value, Key))
			Again.Add(Kv.Value);
	for (const blendity_link::Json& M : Again)
		OnMaterial(M);
}

void FBlendityLinkApplier::OnMaterial(const blendity_link::Json& J)
{
	const int32 Key = (int32)J["key"].number(0);
	MaterialMsgs.Add(Key, J);
	TStrongObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Key);
	UMaterialInstanceDynamic* M = Found && Found->IsValid() ? Found->Get() : nullptr;
	if (!M)
	{
		UMaterialInterface* Base = TexturedBase();
		if (!Base)
			Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Base)
			return;
		M = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
		Materials.Add(Key, TStrongObjectPtr<UMaterialInstanceDynamic>(M));  // (kept while the applier lives, freed with it)
	}
	const blendity_link::Json& C = J["gltf"]["pbrMetallicRoughness"]["baseColorFactor"];
	const FLinearColor Color(C.size() > 0 ? C[0].number(0.8) : 0.8, C.size() > 1 ? C[1].number(0.8) : 0.8, C.size() > 2 ? C[2].number(0.8) : 0.8,
							 C.size() > 3 ? C[3].number(1.0) : 1.0);
	M->SetVectorParameterValue(TEXT("Color"), Color);
	const TStrongObjectPtr<UTexture2D>* BaseMap = Textures.Find((int32)J["maps"]["Base Map"]["texture"].number(0));
	UTexture* White = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	M->SetTextureParameterValue(TEXT("BaseMap"), BaseMap && BaseMap->IsValid() ? BaseMap->Get() : White);  // (a map taken off: white again)
	for (const auto& Kv : Slots)
		if (Kv.Value.Contains(Key))
			AssignMaterials(Kv.Key);
}

void FBlendityLinkApplier::OnLight(const blendity_link::Json& J)
{
	const int64 Id = J["id"].id();
	AActor* Obj = Alive(Id);
	UWorld* W = World.Get();
	if (!Obj || !W)
		return;
	const FString Type(UTF8_TO_TCHAR(J["type"].string().c_str()));
	UClass* Want = Type == TEXT("directional") ? ADirectionalLight::StaticClass() : Type == TEXT("spot") ? ASpotLight::StaticClass() : APointLight::StaticClass();
	TWeakObjectPtr<AActor>* Part = Parts.Find(Id);
	AActor* L = Part && Part->IsValid() ? Part->Get() : nullptr;
	if (L && L->GetClass() != Want)
	{
		L->Destroy();
		L = nullptr;
	}
	if (!L)
	{
		FActorSpawnParameters P;
		P.ObjectFlags |= RF_Transient;
		L = W->SpawnActor<AActor>(Want, P);
		if (!L)
			return;
		L->AttachToActor(Obj, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		L->SetFolderPath(FolderName);
		Parts.Add(Id, L);
	}
	const blendity_link::Json& C = J["color"];
	ULightComponent* LC = L->FindComponentByClass<ULightComponent>();
	if (!LC)
		return;
	LC->SetLightColor(FLinearColor(C[0].number(1), C[1].number(1), C[2].number(1)));
	LC->SetIntensity(J["intensity"].number(1) * (Type == TEXT("directional") ? 3.14159265 : 1000.0));  // (lux for a sun, candela-ish for the others)
	if (UPointLightComponent* PL = Cast<UPointLightComponent>(LC))
		PL->SetAttenuationRadius(J["range"].number(10) * 100.0);
	if (USpotLightComponent* SL = Cast<USpotLightComponent>(LC))
	{
		SL->SetOuterConeAngle(J["spot_angle"].number(30) * 0.5);
		SL->SetInnerConeAngle(J["inner_spot_angle"].number(21.8) * 0.5);
	}
}

void FBlendityLinkApplier::OnCamera(const blendity_link::Json& J)
{
	const int64 Id = J["id"].id();
	AActor* Obj = Alive(Id);
	UWorld* W = World.Get();
	if (!Obj || !W)
		return;
	TWeakObjectPtr<AActor>* Part = Parts.Find(Id);
	ACameraActor* Cam = Part && Part->IsValid() ? Cast<ACameraActor>(Part->Get()) : nullptr;
	if (!Cam)
	{
		FActorSpawnParameters P;
		P.ObjectFlags |= RF_Transient;
		Cam = W->SpawnActor<ACameraActor>(P);
		if (!Cam)
			return;
		Cam->AttachToActor(Obj, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Cam->SetFolderPath(FolderName);
		Parts.Add(Id, Cam);
	}
	UCameraComponent* C = Cam->GetCameraComponent();
	const bool bOrtho = J["orthographic"].boolean();
	C->SetProjectionMode(bOrtho ? ECameraProjectionMode::Orthographic : ECameraProjectionMode::Perspective);
	C->SetOrthoWidth(J["ortho_size"].number(5) * 2.0 * 100.0 * 16.0 / 9.0);
	/* Blendity's fov is vertical, Unreal's horizontal: at 16:9. */
	const double V = FMath::DegreesToRadians(J["fov"].number(60));
	C->SetFieldOfView(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(V * 0.5) * 16.0 / 9.0)));
	C->SetAspectRatio(16.0f / 9.0f);
}

/* ------------------------------------------------------------------ the look (round 376) */

static FLinearColor Col(const blendity_link::Json& C, float R, float G, float B)
{
	return FLinearColor(C[0].number(R), C[1].number(G), C[2].number(B));
}

/* A look actor of an earlier session (a reconnect): taken back by its class, not made again (review 376: three more
 * actors every reconnect, two SkyLights fighting, the fog doubled). */
void FBlendityLinkApplier::AdoptLook(AActor* A)
{
	if (!A || !A->ActorHasTag(FName(TEXT("BlendityKind:look"))))
		return;
	if (A->IsA<ASkyLight>())
		Sky = A;
	if (A->IsA<AExponentialHeightFog>())
		Fog = A;
	if (A->IsA<APostProcessVolume>())
		Post = A;
}

/* One of the look's actors: made once (transient, in the Live Link folder, tagged as Blendity's), found after. */
AActor* FBlendityLinkApplier::LookActor(TWeakObjectPtr<AActor>& Slot, UClass* Class, const TCHAR* Label)
{
	UWorld* W = World.Get();
	if (Slot.IsValid() || !W)
		return Slot.Get();
	FActorSpawnParameters P;
	P.ObjectFlags |= RF_Transient;
	AActor* A = W->SpawnActor<AActor>(Class, P);
	if (!A)
		return nullptr;
	A->Tags.Add(IdTag);
	A->Tags.Add(FName(TEXT("BlendityKind:look")));
	A->SetActorLabel(Label);
	A->SetFolderPath(FolderName);
	Slot = A;
	return A;
}

/* The world: its strength on a SkyLight captured from the level, tinted by the sky colour (a plain colour's), the
 * Gradient's ground as its lower hemisphere (Unreal draws no gradient sky of its own); the distance fog as an
 * ExponentialHeightFog starting where Blendity's starts, its density within Unreal's range and thinner the longer the
 * fog (an approximation: Unreal's fog is exponential). An HDRI isn't loaded here. */
void FBlendityLinkApplier::OnWorld(const blendity_link::Json& J)
{
	const FString Mode(UTF8_TO_TCHAR(J["mode"].string().c_str()));
	if (ASkyLight* S = Cast<ASkyLight>(LookActor(Sky, ASkyLight::StaticClass(), TEXT("Blendity Sky Light"))))
	{
		USkyLightComponent* L = S->GetLightComponent();
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensity(J["strength"].number(1));
		L->SetLightColor(Mode == TEXT("color") ? Col(J["color"], 0.05f, 0.05f, 0.05f) : Col(J["sky"], 0.45f, 0.52f, 0.62f));
		L->bLowerHemisphereIsBlack = false;
		L->SetLowerHemisphereColor(Col(J["ground"], 0.16f, 0.15f, 0.14f));
		L->RecaptureSky();
	}
	const blendity_link::Json& F = J["fog"];
	AExponentialHeightFog* Fg = Cast<AExponentialHeightFog>(LookActor(Fog, AExponentialHeightFog::StaticClass(), TEXT("Blendity Fog")));
	if (!Fg)
		return;
	UExponentialHeightFogComponent* C = Fg->GetComponent();
	const double Start = F["start"].number(10), End = F["end"].number(60);
	C->SetVisibility(F["enabled"].boolean());
	C->SetStartDistance(Start * 100.0);
	C->SetFogDensity(FMath::Clamp(0.5 / FMath::Max(End - Start, 0.1), 0.0, 0.05));  // (Unreal's slider tops at 0.05)
	C->SetFogInscatteringColor(Col(F["color"], 0.6f, 0.65f, 0.75f));
}

void FBlendityLinkApplier::OnRender(const blendity_link::Json& J)
{
	ExposureStops = J["exposure"].number(0);
	ApplyPost();
}

void FBlendityLinkApplier::OnFilters(const blendity_link::Json& J)
{
	Effects = J["effects"];
	GradeStops = 0;
	for (size_t i = 0; i < Effects.size(); i++)
		if (Effects[i]["type"].string() == "Color Grading" && Effects[i]["enabled"].boolean(true))
			GradeStops = Effects[i]["fields"]["Exposure"].number(0);
	ApplyPost();
}

/* An unbound PostProcessVolume: the exposure (the render's and a Color Grading's) as the exposure bias, Bloom, Vignette
 * and the grading's contrast and saturation. Unreal's tone mapper is its own: the view transform isn't changed. */
void FBlendityLinkApplier::ApplyPost()
{
	APostProcessVolume* V = Cast<APostProcessVolume>(LookActor(Post, APostProcessVolume::StaticClass(), TEXT("Blendity Look")));
	if (!V)
		return;
	V->bUnbound = true;
	FPostProcessSettings& S = V->Settings;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = ExposureStops + GradeStops;
	S.bOverride_BloomIntensity = S.bOverride_BloomThreshold = S.bOverride_VignetteIntensity = true;
	S.bOverride_ColorContrast = S.bOverride_ColorSaturation = true;
	S.BloomIntensity = 0, S.VignetteIntensity = 0;
	S.ColorContrast = FVector4(1, 1, 1, 1), S.ColorSaturation = FVector4(1, 1, 1, 1);
	for (size_t i = 0; i < Effects.size(); i++)
	{
		const blendity_link::Json& E = Effects[i];
		const blendity_link::Json& Fd = E["fields"];
		const std::string& Type = E["type"].string();
		if (!E["enabled"].boolean(true))
			continue;
		if (Type == "Bloom")
			S.BloomIntensity = Fd["Intensity"].number(0.6), S.BloomThreshold = Fd["Threshold"].number(1);
		if (Type == "Vignette")
			S.VignetteIntensity = Fd["Intensity"].number(0.4);
		if (Type == "Color Grading")
			S.ColorContrast = FVector4(1, 1, 1, 1 + Fd["Contrast"].number(0)), S.ColorSaturation = FVector4(1, 1, 1, 1 + Fd["Saturation"].number(0));
	}
}
