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

	// This vertex's index into the owning FSWGMeshData's own POSN array (the
	// same space FSWGMeshData::MorphTargets' deltas are keyed by) — needed to
	// look up a morph delta for this specific corner, since a submesh's
	// Vertices don't preserve POSN order (see ReadMgnSubmesh). Only populated
	// for .mgn meshes; INDEX_NONE for .msh (which has no morph data at all).
	int32 SourcePositionIndex = INDEX_NONE;
};

/** One shader-bound submesh: a self-contained vertex buffer + flat triangle index list. */
struct FSWGMeshSubmesh
{
	FString ShaderName;
	TArray<FSWGMeshVertex> Vertices;
	TArray<int32> Triangles; // flat, groups of 3

	/**
	 * Named body regions this submesh's geometry occupies — decoded from the
	 * owning .mgn part's OZN (zone name table) + ZTO (this part's zone
	 * indices) + optional OZC (submesh-index -> zone-index pairs, only
	 * present when a part has more than one PSDT submesh spanning different
	 * zones; every single-submesh part observed so far omits it, in which
	 * case every zone in ZTO applies to that one submesh). Confirmed against
	 * real data: hum_m_body_l3.mgn's single submesh occupies {chest,
	 * torso_f, torso_b, waist_f, waist_b, l_arm, r_arm, l_thigh, r_thigh,
	 * l_shin, r_shin, l_foot, r_foot}; armor_composite_s01_chest_plate_m_l3
	 * .mgn's single submesh occupies {chest, torso_f, torso_b} — an exact
	 * subset, which is how the client hides bare skin under armor without a
	 * literal "hide" flag anywhere in the item template data. Empty for mesh
	 * parts with no OZN/ZTO (most non-humanoid or non-clothing geometry). */
	TArray<FString> OcclusionZoneNames;
};

/**
 * One named blend/morph shape from a .mgn's FORM BLTS (e.g. "blend_fat",
 * "blend_cheeks_0") — sparse position deltas, keyed by POSN index (the same
 * space FSWGMeshVertex::SourcePositionIndex uses), matching this part's own
 * unmorphed Positions array. Only the subset of vertices this shape actually
 * moves has an entry; everything else is implicitly zero delta.
 */
struct FSWGMeshMorphTarget
{
	FString Name;
	TMap<int32, FVector> PositionDeltas;
};

/** One named .mgn hardpoint, stored relative to ParentName. */
struct FSWGMeshHardpoint
{
	FString Name;
	FString ParentName;
	FQuat Rotation = FQuat::Identity;
	FVector Translation = FVector::ZeroVector;
};

struct FSWGMeshData
{
	TArray<FSWGMeshSubmesh> Submeshes;
	TArray<FSWGMeshHardpoint> Hardpoints;

	/** Named blend shapes for this mesh part (see FSWGMeshMorphTarget) — only
	 *  .mgn parts with a FORM BLTS have any; most don't (e.g. hands/arms
	 *  rarely carry face/body blends, those live on the head/body parts). */
	TArray<FSWGMeshMorphTarget> MorphTargets;

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
class SWGANIMATION_API FSWGMeshReader
{
public:
	/** Parses a .msh buffer (FORM MESH). Returns false if the buffer isn't a recognizable static mesh. */
	static bool ReadStaticMesh(const FSWGIffReader& Reader, FSWGMeshData& OutMesh);

	/** Parses a .mgn buffer (FORM SKMG) in bind pose, no skinning applied. Returns false if unrecognized. */
	static bool ReadSkeletalMeshBindPose(const FSWGIffReader& Reader, FSWGMeshData& OutMesh);

	/** Returns the skeleton virtual path stored in a skeletal mesh's SKTM chunk. */
	static FString ReadSkeletalMeshSkeletonPath(const FSWGIffReader& Reader);

	/** Returns every skeleton path in SKTM; head meshes commonly add a face skeleton after the shared body skeleton. */
	static TArray<FString> ReadSkeletalMeshSkeletonPaths(const FSWGIffReader& Reader);

private:
	static FString ReadNullTerminatedString(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk);

	/** Best-effort bounding box from FORM APPR > FORM 0003 > FORM EXBX > FORM 0001 > "BOX ". Leaves OutMesh untouched if any step is missing. */
	static void TryReadAppearance(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, FSWGMeshData& OutMesh);

	/**
	 * Tags every submesh in OutMesh with the SAME flat zone list, read from
	 * the file's own hand-authored OZN (zone name table) + ZTO (this part's
	 * zone-index list) chunks — the reliable, artist-authored "which zones
	 * does this whole part cover" signal, confirmed correct on every
	 * wearable item checked. Preferred over SplitSubmeshesByBoneZone
	 * whenever present: bone-weight dominance (what that function uses
	 * instead) is a skinning concept, not an anatomical-region one, and the
	 * two genuinely diverge near a joint (see that function's own comment).
	 * Returns false (OutMesh untouched) if this part has no ZTO at all —
	 * confirmed so far to only be the Wookiee body/head parts, which fall
	 * back to SplitSubmeshesByBoneZone instead (see ReadSkeletalMeshBindPose).
	 */
	static bool ApplyZtoZonesToAllSubmeshes(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, FSWGMeshData& OutMesh);

	/**
	 * Fallback for parts with no ZTO (see ApplyZtoZonesToAllSubmeshes) —
	 * splits every submesh in OutMesh into one sub-submesh per occlusion
	 * zone group, using each vertex's dominant skin-weight bone (already
	 * decoded into FSWGMeshVertex::BoneWeights by ReadVertexWeights) mapped
	 * through a fixed bone-name -> zone-name(s) table (see
	 * SWGMeshReader.cpp's anonymous namespace). Bone names come from the
	 * shared "appearance/skeleton/all_b.skt" skeleton every species checked
	 * uses, so this table applies uniformly rather than needing per-species
	 * handling. A triangle is assigned to whichever zone group its first
	 * vertex belongs to; vertices with no recognized bone (or no bone
	 * weights at all — a .msh has none) land in an untagged group that's
	 * never a hide candidate. No-op if OutMesh has no BoneNames (i.e. this
	 * wasn't a skeletal mesh part).
	 */
	static void SplitSubmeshesByBoneZone(FSWGMeshData& OutMesh);

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

	/** Decodes FORM HPTS > STAT/DYN records into named, joint-relative hardpoints. */
	static void ReadHardpoints(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, FSWGMeshData& OutMesh);

	/**
	 * Decodes FORM BLTS > FORM BLT (repeated) into OutMesh.MorphTargets, if
	 * present under Form0004 (most parts have none — not an error). Each BLT
	 * is CHUNK INFO {int32 PosnDeltaCount; int32 NormDeltaCount; char Name[]}
	 * followed by CHUNK POSN (PosnDeltaCount * {uint32 PosnIndex; float DX,
	 * DY, DZ}, sparse and not index-sorted) and CHUNK NORM (same shape, for
	 * normal deltas — not currently used; the mesh builder derives normals
	 * for morphed positions on its own). Confirmed via swg.DumpIffTree
	 * against appearance/mesh/hum_m_body_l0.mgn: three BLT children named
	 * "blend_muscle"/"blend_fat"/"blend_skinny", each INFO's first two ints
	 * exactly matching its POSN/NORM chunk sizes / 16.
	 */
	static void ReadBlendTargets(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, FSWGMeshData& OutMesh);
};
