#include "Common/SWGLocomotionResolver.h"

#include "TRE/SWGAshReader.h"
#include "TRE/SWGLatReader.h"

namespace
{
	/**
	 * The .ash node name for a posture. See SWGLocomotionResolver.h for why
	 * this is a hard-coded table rather than something read out of the TREs.
	 *
	 * StateBitmask disambiguates the two postures that map to more than one
	 * node: Sitting is a chair or the ground, and Incapacitated is face-up or
	 * floating, depending on whether the creature is in water.
	 *
	 * Crouched maps to "kneeling", not "crouched" — posture 1 is SWG's /kneel,
	 * whose stationary locomotion movement_human.iff gives as Kneeling. The
	 * "crouched" node belongs to posture 3 (Sneaking), which all_b.ash points
	 * at the same loop_crouched; it's also where Crouched borrows its moving
	 * clips from, see PostureMovingStateName.
	 */
	FString PostureStateName(ESWGPosture Posture, int64 StateBitmask)
	{
		switch (Posture)
		{
			case ESWGPosture::Upright:        return FString();  // the node already reached (root, or a combat/weapon subtree)
			case ESWGPosture::Crouched:       return TEXT("kneeling");
			case ESWGPosture::Prone:          return TEXT("prone");
			case ESWGPosture::Sneaking:       return TEXT("sneaking");
			case ESWGPosture::Blocking:       return TEXT("blocking");
			case ESWGPosture::Climbing:       return TEXT("climbing");
			case ESWGPosture::Flying:         return TEXT("hovering");
			case ESWGPosture::LyingDown:      return TEXT("lying");
			case ESWGPosture::Sitting:        return SWGHasState(StateBitmask, ESWGState::SittingOnChair) ? TEXT("sitting_chair") : TEXT("sitting_ground");
			case ESWGPosture::SkillAnimating: return TEXT("skill");
			case ESWGPosture::DrivingVehicle: return TEXT("riding");
			case ESWGPosture::RidingCreature: return TEXT("riding");
			case ESWGPosture::KnockedDown:    return TEXT("knocked_down");
			case ESWGPosture::Incapacitated:  return SWGHasState(StateBitmask, ESWGState::Swimming) ? TEXT("incapacitated_water") : TEXT("incapacitated_face_up");
			case ESWGPosture::Dead:           return TEXT("incapacitated_face_down");
			default:                          return FString();
		}
	}

	/** Node to borrow moving clips from when the posture's own node has none — empty for postures that need no fallback. See ResolveClipSet's header comment. */
	FString PostureMovingStateName(ESWGPosture Posture)
	{
		return Posture == ESWGPosture::Crouched ? TEXT("crouched") : FString();
	}

	/** Descends from Node into its child named Name, or leaves Node alone if it has no such child. */
	void TryDescend(const FSWGAnimationStateHierarchy& Hierarchy, const FSWGAnimationState*& Node, const FString& Name)
	{
		if (Name.IsEmpty() || !Node)
		{
			return;
		}

		if (const FSWGAnimationState* Child = Hierarchy.FindChildOf(*Node, Name))
		{
			Node = Child;
		}
	}

	/**
	 * Loop animation named by Node, inherited from the nearest ancestor that
	 * names one. "default" is how the .ash spells "no loop of my own here" on
	 * some nodes (sword_2h's blocking, for one), so it inherits like an empty
	 * name does.
	 */
	FString LoopNameOf(const FSWGAnimationStateHierarchy& Hierarchy, const FSWGAnimationState* Node)
	{
		while (Node && (Node->LoopAnimationName.IsEmpty() || Node->LoopAnimationName == TEXT("default")))
		{
			Node = Hierarchy.States.IsValidIndex(Node->ParentIndex) ? &Hierarchy.States[Node->ParentIndex] : nullptr;
		}
		return Node ? Node->LoopAnimationName : FString();
	}

	const FSWGLatClip* FindClipByParameter(const FSWGLatEntry& Entry, const TCHAR* Parameter)
	{
		return Entry.Clips.FindByPredicate([Parameter](const FSWGLatClip& Clip)
			{
				return Clip.ParameterName == Parameter;
			});
	}

	/** The clips an entry cross-fades as the creature speeds up, slowest first — the LAT lists them in ramp order inside the SPAT that holds them. */
	TArray<FString> MovingPathsOf(const FSWGLatEntry& Entry)
	{
		TArray<FString> Paths;
		for (const FSWGLatClip& Clip : Entry.Clips)
		{
			if (Clip.ParameterName == TEXT("locomotion"))
			{
				Paths.Add(Clip.AnsPath);
			}
		}
		return Paths;
	}

	/**
	 * Standing-still clip of an entry. Most loops tag it "zero_speed"; the
	 * crouch loops use "transition" instead. loop_standing tags neither,
	 * because its idle isn't a single clip at all — it's a "mood" selector
	 * over 33 ambient-action sets, each wrapping its own idle — so it falls
	 * through to the last two rules: the specific breathe-normally clip (the
	 * one this pipeline played for standing before postures existed, and still
	 * the right neutral choice), then "whatever isn't a moving clip", which
	 * also covers single-clip loops like blocking and incapacitated.
	 */
	FString IdlePathOf(const FSWGLatEntry& Entry)
	{
		const FSWGLatClip* IdleClip = FindClipByParameter(Entry, TEXT("zero_speed"));
		if (!IdleClip)
		{
			IdleClip = FindClipByParameter(Entry, TEXT("transition"));
		}
		if (!IdleClip)
		{
			IdleClip = Entry.Clips.FindByPredicate([](const FSWGLatClip& Clip)
				{
					return Clip.AnsPath.Contains(TEXT("_idl_breathe_normally"), ESearchCase::IgnoreCase);
				});
		}
		if (!IdleClip)
		{
			IdleClip = Entry.Clips.FindByPredicate([](const FSWGLatClip& Clip)
				{
					return Clip.ParameterName != TEXT("locomotion");
				});
		}
		return IdleClip ? IdleClip->AnsPath : FString();
	}
}

const FSWGAnimationState* SWGLocomotion::ResolveState(const FSWGAnimationStateHierarchy& Hierarchy, ESWGPosture Posture, int64 StateBitmask)
{
	const FSWGAnimationState* Node = Hierarchy.GetRoot();
	if (!Node)
	{
		return nullptr;
	}

	// Combat is a whole parallel subtree that repeats the posture nodes with
	// weapon-ready loops, so it has to be entered before the posture step —
	// otherwise a creature fighting prone would get the peaceful prone loop.
	if (SWGHasState(StateBitmask, ESWGState::Combat))
	{
		TryDescend(Hierarchy, Node, TEXT("combat"));
	}

	// Swimming outranks the posture the server reports (it keeps sending
	// Upright for a swimming creature), except when the creature is down —
	// those postures have their own in-water nodes.
	const bool bDown = Posture == ESWGPosture::Incapacitated || Posture == ESWGPosture::Dead || Posture == ESWGPosture::KnockedDown;
	if (SWGHasState(StateBitmask, ESWGState::Swimming) && !bDown)
	{
		TryDescend(Hierarchy, Node, TEXT("swimming"));
		return Node;
	}

	TryDescend(Hierarchy, Node, PostureStateName(Posture, StateBitmask));
	return Node;
}

FString SWGLocomotion::ResolveTransitionClip(const FSWGAnimationStateHierarchy& Hierarchy, const FSWGLatData& Lat, ESWGPosture FromPosture, ESWGPosture ToPosture, int64 StateBitmask)
{
	const FSWGAnimationState* FromState = ResolveState(Hierarchy, FromPosture, StateBitmask);
	const FSWGAnimationState* ToState = ResolveState(Hierarchy, ToPosture, StateBitmask);
	if (!FromState || !ToState || FromState == ToState)
	{
		return FString();
	}

	const TArray<FString> DestinationPath = Hierarchy.PathTo(*ToState);
	const FSWGAnimationStateLink* Link = FromState->Links.FindByPredicate(
		[&DestinationPath](const FSWGAnimationStateLink& Candidate) { return Candidate.DestinationPath == DestinationPath; });

	if (!Link || Link->TransitionAnimationName.IsEmpty())
	{
		return FString();
	}

	// The transition is a single clip, so take the entry's idle slot — for a
	// one-PXAT entry that is simply the clip itself.
	const FSWGLatEntry* Entry = Lat.Find(Link->TransitionAnimationName);
	return Entry ? IdlePathOf(*Entry) : FString();
}

bool SWGLocomotion::ResolveClipSet(const FSWGAnimationStateHierarchy& Hierarchy, const FSWGLatData& Lat, ESWGPosture Posture, int64 StateBitmask, FSWGLocomotionClipSet& OutClipSet)
{
	OutClipSet = FSWGLocomotionClipSet();

	const FString IdleLoopName = LoopNameOf(Hierarchy, ResolveState(Hierarchy, Posture, StateBitmask));
	const FSWGLatEntry* IdleEntry = Lat.Find(IdleLoopName);
	if (!IdleEntry || IdleEntry->Clips.Num() == 0)
	{
		return false;
	}

	OutClipSet.IdleLoopName = IdleLoopName;
	OutClipSet.MovingLoopName = IdleLoopName;
	OutClipSet.IdlePath = IdlePathOf(*IdleEntry);

	TArray<FString> MovingPaths = MovingPathsOf(*IdleEntry);

	// Posture's own node has no moving clips — borrow them from its movement
	// node, if it has one and that node's own loop supplies any.
	if (MovingPaths.Num() == 0)
	{
		const FSWGAnimationState* Root = Hierarchy.GetRoot();
		const FString MovingStateName = PostureMovingStateName(Posture);
		const FSWGAnimationState* MovingState = (Root && !MovingStateName.IsEmpty()) ? Hierarchy.FindChildOf(*Root, MovingStateName) : nullptr;
		const FString MovingLoopName = MovingState ? LoopNameOf(Hierarchy, MovingState) : FString();

		if (const FSWGLatEntry* MovingEntry = MovingLoopName.IsEmpty() ? nullptr : Lat.Find(MovingLoopName))
		{
			MovingPaths = MovingPathsOf(*MovingEntry);
			if (MovingPaths.Num() > 0)
			{
				OutClipSet.MovingLoopName = MovingLoopName;
			}
		}
	}

	if (OutClipSet.IdlePath.IsEmpty())
	{
		OutClipSet.IdlePath = MovingPaths.Num() > 0 ? MovingPaths[0] : IdleEntry->Clips[0].AnsPath;
	}
	OutClipSet.WalkPath = MovingPaths.Num() > 0 ? MovingPaths[0] : OutClipSet.IdlePath;
	OutClipSet.RunPath = MovingPaths.Num() > 0 ? MovingPaths.Last() : OutClipSet.WalkPath;

	return OutClipSet.IsValid();
}
