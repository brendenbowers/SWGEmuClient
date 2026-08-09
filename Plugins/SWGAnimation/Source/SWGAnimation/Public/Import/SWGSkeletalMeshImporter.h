#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGSkeletonReader.h"
#include "TRE/SWGMeshReader.h"

/**
 * Recovers the real shader virtual path from a built mesh's
 * FSkeletalMaterial::MaterialSlotName — every caller that resolves a
 * generated skeletal mesh's real per-shader materials at runtime
 * (FSWGSkeletalAnimationPipeline::TryApplyGeneratedAnimatedMesh,
 * USWGMeshGeneratorSubsystem's wearable item path) needs this, not just
 * SlotName.ToString() directly: FSWGSkeletalMeshImporter's import step
 * appends a "#<MatIndex>" suffix to keep every section's import material
 * name unique (see PopulateImportData's own comment for why — the mesh
 * builder's chunker groups faces by matching this name, not by the numeric
 * MatIndex, so same-shader sections that must stay visually/hiding-
 * independent, e.g. FSWGMeshReader::SplitSubmeshesByBoneZone's per-zone
 * pieces of one original submesh, would otherwise silently collapse back
 * into one). Strips everything from the last '#' onward; returns SlotName
 * unchanged if it has none (older cached assets built before this suffix
 * existed). Free function, not a FSWGSkeletalMeshImporter method, because
 * callers that need it are NOT WITH_EDITOR-gated (a generated mesh is a
 * runtime asset once built), while that whole class is.
 */
SWGANIMATION_API FString ExtractShaderPathFromMaterialSlotName(const FString& SlotName);

#if WITH_EDITOR

#include "ReferenceSkeleton.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODImporterData.h"

class USkeletalMesh;
class IMeshUtilities;

/** Plain description of one socket to create later — deferred so socket
 *  resolution (which only needs a standalone FReferenceSkeleton and
 *  hardpoint data) can happen on a worker thread, with the actual
 *  NewObject<USkeletalMeshSocket> done on the game thread in
 *  FSWGSkeletalMeshImporter::FinalizeSkeletalMesh. */
struct FSWGSocketDesc
{
	FName SocketName;
	FName BoneName;
	FVector RelativeLocation = FVector::ZeroVector;
	FRotator RelativeRotation = FRotator::ZeroRotator;
};

/** Everything FSWGSkeletalMeshImporter::BuildSkeletalMeshData produces —
 *  plain data only, safe to build on a worker thread and hand over to
 *  FSWGSkeletalMeshImporter::FinalizeSkeletalMesh on the game thread. A
 *  complete type (not just forward-declared) since callers outside this
 *  module (USWGMeshGeneratorSubsystem) need to construct/hold one directly,
 *  not just pass it through by pointer. */
struct FSWGSkeletalMeshBuildData
{
	FSkeletalMeshImportData ImportData;
	FReferenceSkeleton RefSkeleton;
	TArray<FSkeletalMaterial> Materials;
	TArray<FSWGSocketDesc> Sockets;
	TUniquePtr<FSkeletalMeshLODModel> LODModel;
	FSkeletalMeshLODInfo LODInfo;
	FBoxSphereBounds Bounds;
	TMap<FString, TMap<int32, FVector>> MorphDeltas;

	/** One entry per material slot/section, same order as Materials — see
	 *  FSWGMeshSubmesh::OcclusionZoneNames. The caller (FSWGSkeletalAnimation
	 *  Pipeline::DrainCompletedSkeletalMeshBuilds) attaches this to the built
	 *  USkeletalMesh as asset user data so it survives a save/reload; this
	 *  struct itself is plain worker-thread-safe data. */
	TArray<TArray<FString>> OcclusionZoneNamesBySection;
};

/**
 * Builds a real, riggable USkeletalMesh + USkeleton pair from already-parsed
 * .skt/.mgn data (FSWGSkeletonReader/FSWGMeshReader), using the same
 * editor-only import-time APIs the FBX importer uses internally
 * (FSkeletalMeshImportData, SkeletalMeshImportUtils, IMeshBuilderModule) —
 * there is no packaged-build-safe way to construct a real skinned mesh (see
 * world-object-plan.html's "Must work in a packaged/shipping build" box,
 * written when bind-pose-only rendering was the only option). This is
 * editor/PIE-only for now; see SWGEmu.Build.cs's WITH_EDITOR-guarded
 * dependencies for the modules this needs.
 */
class SWGANIMATION_API FSWGSkeletalMeshImporter
{
public:
	/**
	 * MeshParts lets multiple .mgn files that share one skeleton (e.g.
	 * Wookiee body + head) merge into a single skinned mesh, each submesh
	 * becoming its own material slot. Each mesh part's own bone name list
	 * (FSWGMeshData::BoneNames) is matched against Skeleton's joint names
	 * case-insensitively to resolve skin weights to the right skeleton bone.
	 *
	 * PackagePath is a full package path (e.g. "/Game/SWGEmu/Generated/SK_Wookiee");
	 * the new USkeletalMesh and a new USkeleton are both saved there.
	 *
	 * Worker-thread-safe half of the split: parses/builds everything that
	 *  doesn't touch a UObject (import data, standalone FReferenceSkeleton,
	 *  socket descriptors, the actual FMeshUtilities::BuildSkeletalMesh
	 *  geometry pass). PackagePath is only used for logging/build-name
	 *  purposes here — no package or asset is touched by this half. */
	static bool BuildSkeletalMeshData(
		IMeshUtilities& MeshUtilities,
		const FSWGSkeletonData& Skeleton,
		const TArray<const FSWGMeshData*>& MeshParts,
		const TMap<FString, FString>& SlotHardpoints,
		const FString& PackagePath,
		FSWGSkeletalMeshBuildData& OutData);

	/** Game-thread-only half of the split: takes the plain-data result of
	 *  BuildSkeletalMeshData and does all NewObject/package/asset-registry
	 *  work — must run on the game thread. */
	static USkeletalMesh* FinalizeSkeletalMesh(
		FSWGSkeletalMeshBuildData&& Data,
		const FString& PackagePath);

private:
	static bool PopulateImportData(
		const FSWGSkeletonData& Skeleton,
		const TArray<const FSWGMeshData*>& MeshParts,
		const FString& PackagePath,
		class FSkeletalMeshImportData& OutImportData,
		TArray<FString>& OutMaterialSlotNames,
		TMap<FString, TMap<int32, FVector>>& OutMergedMorphDeltas,
		TArray<TArray<FString>>& OutOcclusionZoneNamesBySection);

	/**
	 * Builds real UMorphTarget assets (blend/body-shape sliders — see
	 * FSWGMeshReader::ReadBlendTargets) and registers them on SkeletalMesh.
	 * Must run after IMeshBuilderModule::BuildSkeletalMesh, not before: UE
	 * 5.8's skeletal mesh builder doesn't consume
	 * FSkeletalMeshImportData::MorphTargets (that's the legacy FBX-import
	 * path — the builder now expects morph data via
	 * FSkeletalMeshAttributes/FMeshDescription instead, which this importer
	 * doesn't produce), so morph targets are constructed directly from the
	 * already-built LOD's real render-vertex data instead, via
	 * FSkeletalMeshLODModel::MeshToImportVertexMap (render vertex index ->
	 * the same "point" index MergedMorphDeltas is keyed by).
	 * MergedMorphDeltas is PopulateImportData's own output — see its comment.
	 */
	static void BuildMorphTargets(
		class USkeletalMesh& SkeletalMesh,
		const TMap<FString, TMap<int32, FVector>>& MergedMorphDeltas);
};

#endif // WITH_EDITOR
