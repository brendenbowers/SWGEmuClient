#pragma once

#include "CoreMinimal.h"
#include "Common/SWGPostureTypes.h"

struct FSWGAnimationState;
struct FSWGAnimationStateHierarchy;
struct FSWGLatData;

/**
 * The three .ans clips one posture's speed ramp is built from — what
 * FSWGSkeletalAnimationPipeline turns into a UBlendSpace. Walk/Run collapse
 * onto Idle for postures that can't move (blocking, incapacitated, sitting),
 * which is the correct result, not a failure: the blend space then holds the
 * same clip at every speed.
 */
struct FSWGLocomotionClipSet
{
	/** Logical animation the idle came from, e.g. "loop_kneeling" — for logging. */
	FString IdleLoopName;

	/** Logical animation the moving clips came from. Usually the same as IdleLoopName; see SWGLocomotion::ResolveClipSet for when it isn't. */
	FString MovingLoopName;

	FString IdlePath;
	FString WalkPath;
	FString RunPath;

	bool IsValid() const { return !IdlePath.IsEmpty() && !WalkPath.IsEmpty() && !RunPath.IsEmpty(); }

	/** Clip paths only — two postures resolving to the same three clips need no blend space swap, whatever they're named. */
	bool operator==(const FSWGLocomotionClipSet& Other) const
	{
		return IdlePath == Other.IdlePath && WalkPath == Other.WalkPath && RunPath == Other.RunPath;
	}
	bool operator!=(const FSWGLocomotionClipSet& Other) const { return !(*this == Other); }
};

/**
 * Maps a creature's network state (CREO base3 posture + state bitmask) onto
 * the animation data: posture picks a node in the species' .ash state
 * hierarchy, that node names a logical animation, and the species' .lat
 * resolves that name to actual .ans clips.
 *
 * The posture -> state-name mapping is the one piece of this chain the game
 * data doesn't carry — datatables/include/posture.iff numbers the postures and
 * datatables/movement/movement_human.iff says how fast each one moves, but
 * nothing in the TREs ties posture 2 to the .ash node called "prone". The
 * original client hard-codes it too; the table in ResolveState is that
 * mapping, written to degrade gracefully (an .ash without a given node just
 * keeps its parent's loop) so non-humanoid hierarchies like creature_base.ash
 * still resolve.
 */
namespace SWGLocomotion
{
	/** The deepest state matching Posture/StateBitmask, never null for a non-empty hierarchy (worst case, the root). */
	SWGEMUCLIENT_API const FSWGAnimationState* ResolveState(const FSWGAnimationStateHierarchy& Hierarchy, ESWGPosture Posture, int64 StateBitmask);

	/**
	 * Full posture -> clips resolution.
	 *
	 * Most postures are one .ash node holding both the standing-still clip and
	 * the moving ones. Crouched is the exception the movement datatable
	 * predicts: its stationary locomotion is Kneeling and its slow one is
	 * CrouchWalking, and the .ash splits those across two nodes — "kneeling"
	 * (a single kneel idle, no moving clips at all) and "crouched" (the crouch
	 * idle plus the crouch walk). So when the posture's own node has no moving
	 * clips, the moving half is sourced from a second node, and a kneeling
	 * creature that starts moving crouch-walks instead of sliding.
	 */
	SWGEMUCLIENT_API bool ResolveClipSet(const FSWGAnimationStateHierarchy& Hierarchy, const FSWGLatData& Lat, ESWGPosture Posture, int64 StateBitmask, FSWGLocomotionClipSet& OutClipSet);

	/**
	 * The .ans clip the .ash authors for changing from one posture to another,
	 * or empty when the hierarchy has no link for that pair (many combinations
	 * have none, and the caller should just switch loops directly).
	 *
	 * Played once, unlooped, before the destination's loop starts: the clip is
	 * authored to begin in the source pose and end in the destination one, so
	 * it carries the change rather than hiding it behind a cross-fade.
	 */
	SWGEMUCLIENT_API FString ResolveTransitionClip(const FSWGAnimationStateHierarchy& Hierarchy, const FSWGLatData& Lat, ESWGPosture FromPosture, ESWGPosture ToPosture, int64 StateBitmask);
}
