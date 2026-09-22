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

FString SWGLocomotion::WeaponStateNameForTemplate(const FString& WeaponTemplatePath)
{
	// Matched on directory, not file name: the directory is the weapon class,
	// which is what the .ash branches on. It offers only five armed sets, so
	// the smaller melee classes share the one-handed one and every long gun
	// shares the rifle set — as the original animations are authored.
	struct FWeaponStateMapping
	{
		const TCHAR* PathFragment;
		const TCHAR* StateName;
	};

	static const FWeaponStateMapping Mappings[] =
	{
		{ TEXT("/melee/2h_sword/"), TEXT("sword_2h") },
		{ TEXT("/melee/polearm/"),  TEXT("polearm")  },
		{ TEXT("/melee/1h_sword/"), TEXT("sword_1h") },
		{ TEXT("/melee/knife/"),    TEXT("sword_1h") },
		{ TEXT("/melee/axe/"),      TEXT("sword_1h") },
		{ TEXT("/melee/baton/"),    TEXT("sword_1h") },
		{ TEXT("/ranged/pistol/"),  TEXT("pistol")   },
		{ TEXT("/ranged/rifle/"),   TEXT("rifle")    },
		{ TEXT("/ranged/carbine/"), TEXT("rifle")    },
		{ TEXT("/ranged/heavy/"),   TEXT("rifle")    },
	};

	if (WeaponTemplatePath.IsEmpty())
	{
		return FString();
	}

	const FString Normalised = WeaponTemplatePath.Replace(TEXT("\\"), TEXT("/"));
	for (const FWeaponStateMapping& Mapping : Mappings)
	{
		if (Normalised.Contains(Mapping.PathFragment))
		{
			return Mapping.StateName;
		}
	}

	// Unarmed weapons, thrown, mines and anything unrecognised: the generic
	// combat subtree, which is also what an empty name selects.
	return FString();
}

const FSWGAnimationState* SWGLocomotion::ResolveState(const FSWGAnimationStateHierarchy& Hierarchy, ESWGPosture Posture, int64 StateBitmask, const FString& WeaponStateName)
{
	const FSWGAnimationState* Node = Hierarchy.GetRoot();
	if (!Node)
	{
		return nullptr;
	}

	// The tree is root/<weapon>/combat/<posture>, with root's own "combat"
	// child being the unarmed set — so the weapon step comes first, and an
	// unknown weapon stays at root and picks up unarmed below.
	TryDescend(Hierarchy, Node, WeaponStateName);

	// Combat repeats the posture nodes with weapon-ready loops, so it has to
	// be entered before the posture step — otherwise a creature fighting
	// prone would get the peaceful prone loop.
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

	// The combat and weapon subtrees don't repeat every posture: all_b.ash
	// has "incapacitated_face_down" (Dead) only at the root, so a creature
	// that dies before its Combat bit clears would otherwise stay on the
	// combat-standing loop. A posture missing here is looked for up the
	// ancestors before giving up and keeping the context node.
	//
	// Riding comes from the state, not the posture: Core3's mount command
	// leaves the rider Upright and only sets RIDINGMOUNT (MountCommand.h),
	// so the DrivingVehicle/RidingCreature postures are never actually sent.
	const FString PostureName = SWGHasState(StateBitmask, ESWGState::RidingMount) ? FString(TEXT("riding")) : PostureStateName(Posture, StateBitmask);
	for (const FSWGAnimationState* Ancestor = Node; Ancestor && !PostureName.IsEmpty(); Ancestor = Hierarchy.States.IsValidIndex(Ancestor->ParentIndex) ? &Hierarchy.States[Ancestor->ParentIndex] : nullptr)
	{
		if (const FSWGAnimationState* PostureNode = Hierarchy.FindChildOf(*Ancestor, PostureName))
		{
			return PostureNode;
		}
	}
	return Node;
}

FString SWGLocomotion::ResolveActionClip(const FSWGAnimationStateHierarchy& Hierarchy, const FSWGLatData& Lat, const FSWGAnimationState& State, const FString& ActionName)
{
	if (ActionName.IsEmpty())
	{
		return FString();
	}

	const FString LogicalName = Hierarchy.ResolveAction(State, ActionName);
	if (LogicalName.IsEmpty())
	{
		return FString();
	}

	// One-shot, so the entry's idle slot is the clip — same reasoning as
	// ResolveTransitionClip: a single-PXAT entry is just the clip itself.
	const FSWGLatEntry* Entry = Lat.Find(LogicalName);
	return Entry ? IdlePathOf(*Entry) : FString();
}

FString SWGLocomotion::ResolveTransitionClip(const FSWGAnimationStateHierarchy& Hierarchy, const FSWGLatData& Lat, ESWGPosture FromPosture, int64 FromStateBitmask, ESWGPosture ToPosture, int64 ToStateBitmask)
{
	const FSWGAnimationState* FromState = ResolveState(Hierarchy, FromPosture, FromStateBitmask);
	const FSWGAnimationState* ToState = ResolveState(Hierarchy, ToPosture, ToStateBitmask);
	if (!FromState || !ToState || FromState == ToState)
	{
		return FString();
	}

	const TArray<FString> DestinationPath = Hierarchy.PathTo(*ToState);
	const auto LeadsToDestination = [&DestinationPath](const FSWGAnimationStateLink& Candidate) { return Candidate.DestinationPath == DestinationPath; };

	// Links are authored per node, and a destination that lives higher up
	// the tree may only be linked from up there: root links to
	// incapacitated_face_down with the fall-forward clip, root/combat
	// doesn't link to it at all. An ancestor's link is the same change from
	// a near-enough pose (combat standing vs standing), so it stands in.
	const FSWGAnimationStateLink* Link = nullptr;
	for (const FSWGAnimationState* Source = FromState; Source && !Link; Source = Hierarchy.States.IsValidIndex(Source->ParentIndex) ? &Hierarchy.States[Source->ParentIndex] : nullptr)
	{
		Link = Source->Links.FindByPredicate(LeadsToDestination);
	}

	if (!Link || Link->TransitionAnimationName.IsEmpty())
	{
		return FString();
	}

	// The transition is a single clip, so take the entry's idle slot — for a
	// one-PXAT entry that is simply the clip itself.
	const FSWGLatEntry* Entry = Lat.Find(Link->TransitionAnimationName);
	return Entry ? IdlePathOf(*Entry) : FString();
}

bool SWGLocomotion::ResolveClipSet(const FSWGAnimationStateHierarchy& Hierarchy, const FSWGLatData& Lat, ESWGPosture Posture, int64 StateBitmask, FSWGLocomotionClipSet& OutClipSet, const FString& RiderPose)
{
	OutClipSet = FSWGLocomotionClipSet();

	const FString IdleLoopName = LoopNameOf(Hierarchy, ResolveState(Hierarchy, Posture, StateBitmask));
	const FSWGLatEntry* FoundEntry = Lat.Find(IdleLoopName);
	if (!FoundEntry)
	{
		return false;
	}

	// loop_riding switches its clip on "rider_pose" — pick the mount's branch
	// (seated in a landspeeder vs. astride a bantha) rather than the default.
	FSWGLatEntry SelectedEntry;
	const FSWGLatEntry* IdleEntry = FoundEntry;
	if (FoundEntry->SelectorVariable == TEXT("rider_pose"))
	{
		SelectedEntry.LogicalName = FoundEntry->LogicalName;
		SelectedEntry.Clips = FoundEntry->ClipsFor(RiderPose);
		IdleEntry = &SelectedEntry;
	}
	if (IdleEntry->Clips.Num() == 0)
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
