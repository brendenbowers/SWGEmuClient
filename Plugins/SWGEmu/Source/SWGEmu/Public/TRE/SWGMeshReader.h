#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** One bone influence — BoneIndex is local to the owning FSWGMeshData's BoneNames list, not a skeleton index. */
struct FSWGBoneWeight
{
	int32 BoneIndex = 0;
	float Weight = 0.0f;
};

/** One decoded vertex — position/normal always present, UVs/color depend on the source format. */
struct FSWGMeshVertex
{
	FVector Position = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
	TArray<FVector2D> UVs;
	FColor Color = FColor::White;
	bool bHasColor = false;

	// Only populated for .mgn (skeletal) meshes read via ReadSkeletalMeshBindPose — empty for .msh.
	TArray<FSWGBoneWeight> BoneWeights;
};

/** One shader-bound submesh: a self-contained vertex buffer + flat triangle index list. */
struct FSWGMeshSubmesh
{
	FString ShaderName;
	TArray<FSWGMeshVertex> Vertices;
	TArray<int32> Triangles; // flat, groups of 3
};

struct FSWGMeshData
{
	TArray<FSWGMeshSubmesh> Submeshes;

	/** From the file's own EXBX/BOX chunk, if present. Unset (no extent) otherwise — not required for rendering. */
	FBox BoundingBox = FBox(ForceInit);
	bool bHasBoundingBox = false;

	/**
	 * This mesh's own bone name list (XFNM), only populated for .mgn meshes —
	 * FSWGMeshVertex::BoneWeights' BoneIndex values index into this array, NOT
	 * into a skeleton's joint list directly. Names are as stored in the file
	 * (lowercase, e.g. "lthigh") — matching against a FSWGSkeletonData's joint
	 * names (e.g. "lThigh") needs a case-insensitive comparison.
	 */
	TArray<FString> BoneNames;
};

/**
 * Parses SWG's .msh (static, FORM MESH) and .mgn (skeletal, FORM SKMG) mesh formats
 * into engine-agnostic geometry data, read directly from TRE bytes at runtime —
 * see world-object-plan.html "Mesh rendering" for the confirmed chunk layouts this
 * is built from. Output feeds a later UDynamicMeshComponent-building step (not
 * implemented yet — this class only decodes geometry, it doesn't touch UE mesh
 * components at all).
 *
 * .mgn's POSN/NORM are read as-is with no skinning APPLIED (no vertex ever
 * gets moved/rotated by a bone) — but per-vertex bone weights (TWHD/TWDT) and
 * this mesh's own bone name list (XFNM) are decoded into FSWGMeshData for a
 * downstream skeletal-mesh-build step to consume, alongside FSWGSkeletonReader's
 * bind pose (see world-object-plan.html "Decision: bind-pose-only creatures/
 * players for now" — that decision predates the current skeleton-import work
 * and is being revisited).
 */
class SWGEMU_API FSWGMeshReader
{
public:
	/** Parses a .msh buffer (FORM MESH). Returns false if the buffer isn't a recognizable static mesh. */
	static bool ReadStaticMesh(const FSWGIffReader& Reader, FSWGMeshData& OutMesh);

	/** Parses a .mgn buffer (FORM SKMG) in bind pose, no skinning applied. Returns false if unrecognized. */
	static bool ReadSkeletalMeshBindPose(const FSWGIffReader& Reader, FSWGMeshData& OutMesh);

private:
	/** Finds the first FORM child with the given FormType among Parent's direct children. */
	static bool FindChildForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& FormType, FSWGIffChunk& OutChunk);

	/** Finds the first leaf chunk with the given Tag among Parent's direct children. */
	static bool FindChildChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& Tag, FSWGIffChunk& OutChunk);

	/** All FORM children (any FormType) among Parent's direct children. */
	static TArray<FSWGIffChunk> FindChildForms(const FSWGIffReader& Reader, const FSWGIffChunk& Parent);

	static FString ReadNullTerminatedString(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk);

	/** Best-effort bounding box from FORM APPR > FORM 0003 > FORM EXBX > FORM 0001 > "BOX ". Leaves OutMesh untouched if any step is missing. */
	static void TryReadBoundingBox(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, FSWGMeshData& OutMesh);

	static bool ReadMshSubmesh(const FSWGIffReader& Reader, const FSWGIffChunk& SubmeshForm, FSWGMeshSubmesh& OutSubmesh);
	static bool ReadMgnSubmesh(const FSWGIffReader& Reader, const FSWGIffChunk& PsdtForm, const TArray<FVector>& Positions, const TArray<FVector>& Normals,
		const TArray<TArray<FSWGBoneWeight>>& VertexWeights, FSWGMeshSubmesh& OutSubmesh);

	/** Splits a chunk of back-to-back null-terminated strings (XFNM) into names, consuming the whole chunk. */
	static TArray<FString> ReadAllNullTerminatedStrings(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk);

	/**
	 * Decodes TWHD (per-vertex influence count) + TWDT (sequential bone-index/
	 * weight pairs) into one weight list per vertex, indexed the same way as
	 * Positions/Normals (by POSN index) — see world-object-plan.html's TWHD/TWDT
	 * entry. Returns an empty array (not a failure) if either chunk is missing;
	 * bind-pose rendering doesn't depend on skin weights being present.
	 */
	static TArray<TArray<FSWGBoneWeight>> ReadVertexWeights(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, int32 VertexCount);
};
