#pragma once

#include "CoreMinimal.h"

class FSWGIffReader;

/**
 * One authored transition out of a state: the destination state's path from
 * the root, and the logical animation that plays to get there.
 *
 * The clip starts in the source state's pose and ends in the destination's,
 * which is how the original client changes posture without any cross-fade —
 * a character visibly kneels down rather than dissolving into the kneel.
 */
struct FSWGAnimationStateLink
{
	/** Destination state path from the root, e.g. ["root", "combat", "kneeling"]. */
	TArray<FString> DestinationPath;

	/** Logical animation to play during the change, e.g. "trn_stand_to_kneeling". Empty when the link authorises the change but supplies no clip. */
	FString TransitionAnimationName;
};

/** One node of the animation state hierarchy — a state the creature can be in, and the logical animation it loops while in it. */
struct FSWGAnimationState
{
	/** Node name, e.g. "root", "prone", "combat", "sitting_chair". Unique only among siblings, not across the tree. */
	FString Name;

	/** Logical animation played on loop in this state, e.g. "loop_prone" — look this up in the species' FSWGLatData. */
	FString LoopAnimationName;

	int32 ParentIndex = INDEX_NONE;
	TArray<int32> ChildIndices;

	/** Authored transitions out of this state, from its FORM LNKS. */
	TArray<FSWGAnimationStateLink> Links;
};

/**
 * A decoded .ash (animation state hierarchy): the tree of states a creature's
 * animation can be in, keyed by name. The tree's shape encodes context —
 * "combat" hangs under "root" and repeats posture states like "kneeling"
 * beneath itself with combat-specific loops, and weapon subtrees ("rifle",
 * "pistol") do the same again.
 */
struct FSWGAnimationStateHierarchy
{
	/** States[0] is the root. */
	TArray<FSWGAnimationState> States;

	/**
	 * Name -> state index, breadth-first so a name that repeats at several
	 * depths resolves to the shallowest (most general) one. Callers that want
	 * a context-specific variant should walk from a known parent instead —
	 * see FindChildOf.
	 */
	TMap<FString, int32> ByName;

	const FSWGAnimationState* GetRoot() const { return States.Num() > 0 ? &States[0] : nullptr; }

	const FSWGAnimationState* Find(const FString& Name) const
	{
		const int32* Index = ByName.Find(Name);
		return Index ? &States[*Index] : nullptr;
	}

	/** The child of Parent named Name, or null — the way to reach e.g. combat's own "kneeling" rather than root's. */
	const FSWGAnimationState* FindChildOf(const FSWGAnimationState& Parent, const FString& Name) const
	{
		for (int32 ChildIndex : Parent.ChildIndices)
		{
			if (States[ChildIndex].Name == Name)
			{
				return &States[ChildIndex];
			}
		}
		return nullptr;
	}

	/** Path from the root down to State, as the names FSWGAnimationStateLink::DestinationPath is written in. */
	TArray<FString> PathTo(const FSWGAnimationState& State) const
	{
		TArray<FString> Path;
		for (const FSWGAnimationState* Node = &State; Node; Node = States.IsValidIndex(Node->ParentIndex) ? &States[Node->ParentIndex] : nullptr)
		{
			Path.Insert(Node->Name, 0);
		}
		return Path;
	}
};

/**
 * Parses SWG's .ash format: FORM ASHT > FORM 0002 > a recursive FORM STAT
 * tree. Each STAT's INFO is [stateName][loopAnimationName], followed by
 * optional FORM ACTS (named one-shot actions), FORM LNKS (transition
 * animations between states) and FORM CHLD (nested STATs).
 *
 * Only the state tree and its loop animations are decoded — ACTS and LNKS are
 * skipped, since nothing drives one-shot actions or plays transition clips
 * yet.
 */
class SWGANIMATION_API FSWGAshReader
{
public:
	static bool ReadHierarchy(const FSWGIffReader& Reader, FSWGAnimationStateHierarchy& OutHierarchy);

private:
	FSWGAshReader() = default;
};
