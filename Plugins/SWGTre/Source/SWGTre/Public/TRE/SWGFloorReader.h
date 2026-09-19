#pragma once

#include "CoreMinimal.h"
class FSWGIffReader;

/**
 * One walkable-floor triangle. Confirmed byte-exact (60 bytes, packed, no
 * padding) across 3 player-house .flr samples, 92 total triangles, zero
 * malformed records: int32 CornerIndex1/2/3, int32 Index (self-index —
 * matches this triangle's own position in the TRIS array), int32
 * NeighborIndex1/2/3 (-1 = no neighbor across that edge, an exterior
 * boundary), float32[3] Normal, byte EdgeType1/2/3, byte FallThrough
 * (0/1 as a byte, not padding), int32 PartTag, int32 PortalId1/2/3
 * (-1 = this edge isn't a portal boundary).
 */
struct FSWGFloorTriangle
{
	int32 CornerIndex1 = 0;
	int32 CornerIndex2 = 0;
	int32 CornerIndex3 = 0;
	int32 Index = 0;
	int32 NeighborIndex1 = -1;
	int32 NeighborIndex2 = -1;
	int32 NeighborIndex3 = -1;
	FVector Normal = FVector::ZeroVector;
	uint8 EdgeType1 = 0;
	uint8 EdgeType2 = 0;
	uint8 EdgeType3 = 0;
	bool  bFallThrough = false;
	int32 PartTag = 0;
	int32 PortalId1 = -1;
	int32 PortalId2 = -1;
	int32 PortalId3 = -1;
};

struct FSWGFloorData
{
	TArray<FVector> Vertices;
	TArray<FSWGFloorTriangle> Triangles;
};

/**
 * Reads a .flr walkable-floor file — FORM FLOR > FORM 0006 > CHUNK VERT +
 * CHUNK TRIS + FORM BTRE + CHUNK BEDG + FORM PGRF. Referenced by a .pob
 * cell's CollisionFloorPath
 *
 * VERT is plain vertex positions: int32 count + float32[3] per vertex —
 * the same layout as a cell's embedded CMSH collision mesh. TRIS is
 * int32 count + that many packed FSWGFloorTriangle records 
 *
 * Only FORM 0006 is decoded and supported. A second, older layout (FORM
 * 0003, 92-byte TRIS records — seen on non-player-house content, e.g.
 * appearance/collision/mun_corl_hospital_s01_r7_collision_floor0.flr) also
 * exists in the corpus but hasn't been decoded — ReadFloor returns false
 * for it rather than silently misreading it as 0006.
 */
/** A floor triangle's edge flags. Edge N runs from corner N to corner N+1 (Core3 FloorMesh). */
enum class ESWGFloorEdgeType : uint8
{
	/** A wall: nothing walks off the floor here. */
	Uncrossable = 0,
	/** Shared with a neighbouring triangle, or a portal into the next cell. */
	Crossable = 1,
	/** Core3's "blocking" edges — kept distinct; treated as a wall too. */
	Blocking = 2,
};

class SWGTRE_API FSWGFloorReader
{
public:
	static bool ReadFloor(const FSWGIffReader& Reader, FSWGFloorData& OutFloor);

	/**
	 * Vertical barrier quads along every uncrossable edge, Height tall, as a
	 * triangle list — how SWG kept you inside a room: the floor ends at the
	 * wall and its edge says so; there is no wall mesh to hit. Core3 builds
	 * the same barriers (FloorMesh, BARRIER_HEIGHT 3 m) for its collision.
	 * Appended to OutVertices/OutIndices, which may already hold the floor.
	 */
	static int32 AppendBarrierMesh(const FSWGFloorData& Floor, float Height, TArray<FVector>& OutVertices, TArray<int32>& OutIndices);

	/**
	 * The walkable triangles as an index list wound so the up-facing side is
	 * the front face (Chaos: (C-A)x(B-A) points out of the front). For a
	 * one-sided floor collider — every .flr triangle is walkable from above
	 * and nothing should ever be stopped by one from below.
	 */
	static void AppendFloorTriangles(const FSWGFloorData& Floor, TArray<int32>& OutIndices);

private:
	FSWGFloorReader() = default;
};
