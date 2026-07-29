#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGSkeletonReader.h"
#include "TRE/SWGMeshReader.h"

#if WITH_EDITOR

class USkeletalMesh;

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
	 * Returns nullptr and logs the reason on failure.
	 */
	static USkeletalMesh* BuildSkeletalMesh(
		const FSWGSkeletonData& Skeleton,
		const TArray<const FSWGMeshData*>& MeshParts,
		const FString& PackagePath,
		const TMap<FString, FString>& SlotHardpoints = {});

private:
	static bool PopulateImportData(
		const FSWGSkeletonData& Skeleton,
		const TArray<const FSWGMeshData*>& MeshParts,
		const FString& PackagePath,
		class FSkeletalMeshImportData& OutImportData,
		TArray<FString>& OutMaterialSlotNames,
		TMap<FString, TMap<int32, FVector>>& OutMergedMorphDeltas);

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
