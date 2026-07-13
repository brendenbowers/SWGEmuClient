#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * One node in a world snapshot's object tree — a static, persistent world
 * object (building, cell, installation, etc.) placed once at server/client
 * data-load time, never sent over the network. Mirrors Core3's own
 * WorldSnapshotNode exactly (MMOCoreORB/src/templates/snapshot/WorldSnapshotNode.h),
 * confirmed against its parse() — same field order, same recursive child
 * structure (a cell is a child node of its owning building, etc).
 */
struct FSWGWorldSnapshotNode
{
	uint32 ObjectID = 0;
	uint32 ParentID = 0;

	/** Index into FSWGWorldSnapshotData::ObjectTemplateNames for this node's template path. */
	uint32 NameID = 0;

	uint32 CellID = 0;

	/** Wire order (w,x,y,z) — matches Core3's Quaternion::set(qw,qx,qy,qz) exactly, no axis remap needed here. */
	FQuat Direction = FQuat::Identity;

	/** Wire order is X,Z,Y (Core3 reads x,z,y then position.set(x,z,y)) — same convention as CmdStartScene's spawn position; no remap needed for UE's X,Y,Z. */
	FVector Position = FVector::ZeroVector;

	float GameObjectType = 0.0f;
	uint32 Unknown2 = 0;

	TArray<FSWGWorldSnapshotNode> Children;
};

/** Everything parsed from one snapshot/<zone>.ws file. */
struct FSWGWorldSnapshotData
{
	/** Top-level nodes only (buildings, world-placed installations, etc — no parent). */
	TArray<FSWGWorldSnapshotNode> Nodes;

	/** Indexed by FSWGWorldSnapshotNode::NameID — the object template path, e.g. "object/building/tatooine/shared_bestine_starport.iff". */
	TArray<FString> ObjectTemplateNames;
};

/**
 * Parses a world snapshot (.ws) file — the client-side counterpart to Core3's
 * own snapshot/<zone>.ws loading (PlanetManagerImplementation::loadSnapshotObjects,
 * which is how the SERVER places every static building/cell at zone startup).
 * Confirmed via TreSubsystem::FileExists that these files are present in our
 * own TRE archives (one per planet, e.g. snapshot/tatooine.ws) — static world
 * content like this is client-known, like terrain, and never sent over the
 * network; there's no SceneCreateObjectByCrc equivalent for it.
 */
class SWGTRE_API FSWGWorldSnapshotReader
{
public:
	/** Parses the full FORM WSNP structure from an already-loaded .ws file's IFF reader. */
	static bool ReadWorldSnapshot(const FSWGIffReader& Reader, FSWGWorldSnapshotData& OutData);

private:
	/** Parses one FORM NODE (and recurses into its own child NODE forms). */
	static bool ReadNode(const FSWGIffReader& Reader, const FSWGIffChunk& NodeForm, FSWGWorldSnapshotNode& OutNode);
};
