#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** One decoded vertex — position/normal always present, UVs/color depend on the source format. */
struct FSWGMeshVertex
{
	FVector Position = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
	TArray<FVector2D> UVs;
	FColor Color = FColor::White;
	bool bHasColor = false;
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
};

/**
 * Parses SWG's .msh (static, FORM MESH) and .mgn (skeletal, FORM SKMG) mesh formats
 * into engine-agnostic geometry data, read directly from TRE bytes at runtime —
 * see world-object-plan.html "Mesh rendering" for the confirmed chunk layouts this
 * is built from. Output feeds a later UDynamicMeshComponent-building step (not
 * implemented yet — this class only decodes geometry, it doesn't touch UE mesh
 * components at all).
 *
 * .mgn decode is bind-pose only: POSN/NORM are read as-is with no skinning applied
 * (see world-object-plan.html "Decision: bind-pose-only creatures/players for now").
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
	static bool ReadMgnSubmesh(const FSWGIffReader& Reader, const FSWGIffChunk& PsdtForm, const TArray<FVector>& Positions, const TArray<FVector>& Normals, FSWGMeshSubmesh& OutSubmesh);
};
