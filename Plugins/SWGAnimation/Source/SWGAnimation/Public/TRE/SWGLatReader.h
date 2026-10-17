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

	/**
	 * Bone mask the clip is restricted to (PPTR's second string, e.g.
	 * "appearance/mask/mask_upper_body.iff"); empty when the clip drives the
	 * whole body. Read it with FSWGMaskReader.
	 */
	FString MaskPath;

	/** Product of the TSCL wrappers (TimeScaleSkeletalAnimation) enclosing the clip: its playback-rate multiplier. 1 when none. */
	float TimeScale = 1.0f;
};

/** One entry of an AGAT (ActionGeneratorSkeletalAnimation) table: an .ash action name and its relative weight. */
struct FSWGLatIdleAction
{
	FString ActionName;
	float Weight = 0.0f;
};

/** One logical animation name and the clips it resolves to, in file order. */
struct FSWGLatEntry
{
	FString LogicalName;

	/** The clips of the default branch wherever a selector sits in the tree (its DFLT). */
	TArray<FSWGLatClip> Clips;

	/**
	 * Set when the entry's own template is a string selector (FORM SSAT) —
	 * the runtime variable it switches on, e.g. "rider_pose" for loop_riding.
	 * Empty for every other entry.
	 */
	FString SelectorVariable;

	/** That selector's branches keyed by value ("vehicle_landspeeder", "default", ...) — from its VAL table. */
	TMap<FString, TArray<FSWGLatClip>> ClipsBySelectorValue;

	/** TSCL wrapper (TimeScaleSkeletalAnimation): playback-rate multiplier of the entry's first clip (each clip also carries its own). 1 when none. */
	float TimeScale = 1.0f;

	/**
	 * DRAT wrapper (DirectionSkeletalAnimation): the clips of each authored
	 * movement direction, keyed by DIR code (SWGAnimLayer::Direction). Empty
	 * for non-directional entries. Clips holds the Forward branch (or the first
	 * authored one when there is no Forward).
	 */
	TMap<uint8, TArray<FSWGLatClip>> ClipsByDirection;

	/** YWAT wrapper (YawSkeletalAnimation): turn-in-place clips. Clips holds the NONE branch; these hold YNEG (turning left) and YPOS (turning right). */
	TArray<FSWGLatClip> YawNegativeClips;
	TArray<FSWGLatClip> YawPositiveClips;

	/** AGAT wrapper: ambient action table and the min/max seconds between actions. */
	TArray<FSWGLatIdleAction> IdleActions;
	float IdleActionMinSeconds = 0.0f;
	float IdleActionMaxSeconds = 0.0f;

	bool IsDirectional() const { return ClipsByDirection.Num() > 0; }
	bool HasYawClips() const { return YawNegativeClips.Num() > 0 || YawPositiveClips.Num() > 0; }

	/** The clips for SelectorValue, falling back to the "default" value's branch, then to Clips. */
	const TArray<FSWGLatClip>& ClipsFor(const FString& SelectorValue) const
	{
		if (const TArray<FSWGLatClip>* Selected = ClipsBySelectorValue.Find(SelectorValue))
		{
			return *Selected;
		}
		if (const TArray<FSWGLatClip>* Default = ClipsBySelectorValue.Find(TEXT("default")))
		{
			return *Default;
		}
		return Clips;
	}
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
 *
 * Wrapper layouts (see Docs/RetailAnimationNotes.md), all inside a FORM 0000:
 *   TSCL: INFO(float scale), one child template.
 *   DRAT: INFO(u8 direction count), FORM DIR  x n { INFO(u8 code), child template }.
 *   YWAT: FORM YNEG / YPOS / NONE, each wrapping a child template.
 *   AGAT: INFO(float min, float max seconds), ACTN(u16 count, {name, float weight}), FORM LOOP { child }.
 * They fill the matching FSWGLatEntry fields; Clips stays the default branch.
 */
class SWGANIMATION_API FSWGLatReader
{
public:
	static bool ReadLat(const FSWGIffReader& Reader, FSWGLatData& OutData);

private:
	FSWGLatReader() = default;
};
