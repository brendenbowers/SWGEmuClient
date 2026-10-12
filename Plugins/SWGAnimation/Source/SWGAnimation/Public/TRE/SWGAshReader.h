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

/**
 * One FORM ACTS entry, a bare ACTN or a FORM MVAC (EditableMovementAnimationAction,
 * SWGEmu.exe 0x7f6cb0). An ACTN is [action name][logical animation][bool][bool];
 * the second bool is useAddTrack (0x7f7d60) — the action plays on the controller's
 * "add" track over the "loop" track instead of replacing it. The first bool is
 * zero in every shipped .ash. An MVAC holds two ACTNs and plays the second while
 * the creature has non-zero velocity (0x7f6f80/0x7f7150), the first otherwise.
 */
struct FSWGAnimationAction
{
	FString ActionName;

	/** Logical animation played when stationary (or always, for a bare ACTN). */
	FString LogicalName;
	bool bUseAddTrack = false;

	/** The MVAC's moving variant; empty for a bare ACTN. Its machine name ("action45") is dropped. */
	FString MovingLogicalName;
	bool bMovingUseAddTrack = false;

	bool HasMovingVariant() const { return !MovingLogicalName.IsEmpty() && MovingLogicalName != TEXT("default"); }
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

	/**
	 * FORM ACTS: action name -> logical animation. The indirection that makes
	 * one combat action mean different clips per weapon — "attack_mid_center_0"
	 * is "pole_standing_ready_thrust_high" under polearm. A value of "default"
	 * means no override here; see ResolveAction.
	 */
	TMap<FString, FString> Actions;

	/** The same actions with their track details; Actions is the Logical name of each entry here. */
	TMap<FString, FSWGAnimationAction> ActionDetails;
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

	/**
	 * The logical animation ActionName means in State, or empty if undefined.
	 * Walks up to the root so the most specific context wins, skipping
	 * "default" — returning it would shadow the parent's real clip with a name
	 * no .lat can resolve.
	 */
	FString ResolveAction(const FSWGAnimationState& State, const FString& ActionName) const
	{
		for (const FSWGAnimationState* Node = &State; Node; Node = States.IsValidIndex(Node->ParentIndex) ? &States[Node->ParentIndex] : nullptr)
		{
			if (const FString* Found = Node->Actions.Find(ActionName))
			{
				if (!Found->IsEmpty() && *Found != TEXT("default"))
				{
					return *Found;
				}
			}
		}
		return FString();
	}

	/** ResolveAction's counterpart that returns the whole entry (track flags, moving variant); null when undefined. */
	const FSWGAnimationAction* ResolveActionDetail(const FSWGAnimationState& State, const FString& ActionName) const
	{
		for (const FSWGAnimationState* Node = &State; Node; Node = States.IsValidIndex(Node->ParentIndex) ? &States[Node->ParentIndex] : nullptr)
		{
			if (const FSWGAnimationAction* Found = Node->ActionDetails.Find(ActionName))
			{
				if (!Found->LogicalName.IsEmpty() && Found->LogicalName != TEXT("default"))
				{
					return Found;
				}
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
 * ACTS is an INFO holding a uint16 count, then one entry per action. An entry
 * is either a bare ACTN chunk — [actionName][logicalAnimationName] as two
 * null-terminated strings plus two trailing flag bytes — or a FORM MVAC
 * wrapping that ACTN together with a second one naming an additive overlay
 * clip. Combat states put nearly all of their actions in MVACs.
 */
class SWGANIMATION_API FSWGAshReader
{
public:
	static bool ReadHierarchy(const FSWGIffReader& Reader, FSWGAnimationStateHierarchy& OutHierarchy);

private:
	FSWGAshReader() = default;
};
