#pragma once

#include "CoreMinimal.h"

class FSWGIffReader;

/**
 * One .ans clip referenced by a logical animation, with the blend parameter
 * the LAT tags it with. For the loop_* entries that drive locomotion the tags
 * are "zero_speed" (the standing-still clip) and "locomotion" (the clips the
 * engine cross-fades as the creature speeds up, slowest first).
 */
struct FSWGLatClip
{
	FString AnsPath;
	FString ParameterName;
};

/** One logical animation name and the clips it resolves to, in file order. */
struct FSWGLatEntry
{
	FString LogicalName;
	TArray<FSWGLatClip> Clips;
};

/**
 * A decoded .lat (logical animation table): the indirection between the names
 * the animation state hierarchy uses ("loop_standing", "loop_prone",
 * "trn_stand_to_kneeling") and the actual .ans files for one species/skeleton.
 * The SAT's LATX chunk is what points a skeleton at its LAT — see
 * USWGMeshGeneratorSubsystem::ResolveMeshPathForTemplate.
 */
struct FSWGLatData
{
	/** The animation state hierarchy this table is written against, e.g. "appearance/ash/all_b.ash". */
	FString AshPath;

	/** Keyed by logical animation name. */
	TMap<FString, FSWGLatEntry> Entries;

	const FSWGLatEntry* Find(const FString& LogicalName) const { return Entries.Find(LogicalName); }
};

/**
 * Parses SWG's .lat format: FORM LATT > FORM 0000 > INFO (the .ash path) plus
 * one FORM ANIM per logical name. Each ANIM holds an INFO with the logical
 * name and then one wrapper form describing how its clips combine — SPAT
 * (speed-blended, what the loop_* locomotion entries use), PXAT (a single
 * clip), TSCL/DRAT/AGAT (time-scaled / directional / action-gated variants).
 *
 * The wrapper kinds differ only in how the engine picks between the clips at
 * runtime, and all of them bottom out in the same FORM PXAT > FORM 0000 >
 * INFO(.ans path) + PUNF(parameter name) shape — so this reader flattens
 * every PXAT under an ANIM into an ordered clip list and keeps each clip's
 * PUNF parameter name, which is what tells the two apart where it matters.
 */
class SWGANIMATION_API FSWGLatReader
{
public:
	static bool ReadLat(const FSWGIffReader& Reader, FSWGLatData& OutData);

private:
	FSWGLatReader() = default;
};
