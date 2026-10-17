#include "Misc/AutomationTest.h"

#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGCombatManagerReader.h"
#include "TRE/SWGAshReader.h"
#include "TRE/SWGLatReader.h"
#include "TRE/SWGAnimationReader.h"
#include "TRE/SWGCrc32.h"
#include "Common/SWGLocomotionResolver.h"
#include "Common/SWGPostureTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Walks the real game files end to end: the animation name the server hashes,
 * through combat/combat_manager.iff, through the .ash action table, to a .ans
 * clip path in the .lat.
 *
 * Reads from the configured TRE directory, so it needs the game data present
 * and is filtered accordingly. These are the two parsers with no synthetic
 * fixture worth writing — their whole value is that they agree with what SOE
 * actually shipped.
 */
namespace
{
	/** A TRE subsystem outside the game-instance lifecycle, so this runs without a world. */
	USWGTreSubsystem* LoadTreForCombatAnimationChainTest(FAutomationTestBase& Test)
	{
		// The subsystem's ClassWithin is UGameInstance, so it needs one as its
		// outer even though nothing here uses the game instance for anything.
		UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
		USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
		if (!Tre)
		{
			Test.AddError(TEXT("could not construct a TRE subsystem"));
			return nullptr;
		}

		Tre->AddToRoot();
		Tre->LoadConfig();

		if (!Tre->LoadArchives())
		{
			Test.AddError(TEXT("no .tre archives loaded — check TreDirectory in DefaultGame.ini"));
			Tre->RemoveFromRoot();
			return nullptr;
		}

		return Tre;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGCombatManagerChainTest,
	"SWGEmu.Animation.CombatChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGCombatManagerChainTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForCombatAnimationChainTest(*this);
	if (!Tre)
	{
		return false;
	}

	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// ── combat_manager.iff ────────────────────────────────────────────────
	FSWGCombatManagerData CombatManager;
	if (!TestTrue(TEXT("combat/combat_manager.iff parses"),
		FSWGCombatManagerReader::ReadCombatManager(Tre->CreateIffReader(TEXT("combat/combat_manager.iff")), CombatManager)))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("combat_manager: %d entries"), CombatManager.Entries.Num()));
	TestTrue(TEXT("has a substantial number of entries"), CombatManager.Entries.Num() > 100);

	// The name the server composes for an unmodified melee swing. The trailing
	// index is the server's own (System::random(3) in
	// CombatQueueCommand::getDefaultAttackAnimation), so it is part of the key.
	const FString ServerAnimationName = TEXT("attack_mid_center_light_0");
	const FSWGCombatManagerEntry* Entry = CombatManager.FindByName(ServerAnimationName);
	if (!TestNotNull(TEXT("attack_mid_center_light_0 has an entry"), Entry))
	{
		return false;
	}

	// The lookup that matters at runtime is by hash, since that is all the
	// wire carries.
	TestEqual(TEXT("hash lookup finds the same entry"),
		CombatManager.FindByCrc(FSWGCrc32::HashString(ServerAnimationName)), Entry);

	// A landed hit on an upright defender.
	const FSWGCombatActionAnimation* OnHit = Entry->Resolve(/*hit*/ 1, (uint8)ESWGPosture::Upright);
	if (!TestNotNull(TEXT("resolves an animation for a landed hit"), OnHit))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("hit: script='%s' attacker='%s' defender='%s'"),
		*OnHit->PlaybackScript, *OnHit->AttackerAction, *OnHit->DefenderAction));

	TestFalse(TEXT("the attacker has an action"), OnHit->AttackerAction.IsEmpty());
	TestFalse(TEXT("a landed hit gives the defender a reaction"), OnHit->DefenderAction.IsEmpty());

	// The dispatch is on the hit result, so a dodge must resolve to something
	// different from a hit — that is the whole point of the outer arm.
	if (const FSWGCombatActionAnimation* OnDodge = Entry->Resolve(/*dodge*/ 3, (uint8)ESWGPosture::Upright))
	{
		AddInfo(FString::Printf(TEXT("dodge: defender='%s'"), *OnDodge->DefenderAction));
		TestNotEqual(TEXT("a dodge reacts differently from a hit"), OnDodge->DefenderAction, OnHit->DefenderAction);
	}

	// An incapacitated defender has no reaction to play.
	if (const FSWGCombatActionAnimation* OnDowned = Entry->Resolve(/*hit*/ 1, (uint8)ESWGPosture::Incapacitated))
	{
		TestTrue(TEXT("an incapacitated defender plays no reaction"), OnDowned->DefenderAction.IsEmpty());
		TestFalse(TEXT("the attacker still swings at it"), OnDowned->AttackerAction.IsEmpty());
	}

	// ── .ash action table + .lat ──────────────────────────────────────────
	FSWGAnimationStateHierarchy Hierarchy;
	if (!TestTrue(TEXT("appearance/ash/all_b.ash parses"),
		FSWGAshReader::ReadHierarchy(Tre->CreateIffReader(TEXT("appearance/ash/all_b.ash")), Hierarchy)))
	{
		return false;
	}

	FSWGLatData Lat;
	if (!TestTrue(TEXT("appearance/lat/all_m.lat parses"),
		FSWGLatReader::ReadLat(Tre->CreateIffReader(TEXT("appearance/lat/all_m.lat")), Lat)))
	{
		return false;
	}

	int32 StatesWithActions = 0;
	for (const FSWGAnimationState& State : Hierarchy.States)
	{
		StatesWithActions += State.Actions.Num() > 0 ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("all_b.ash: %d states, %d of them with actions"), Hierarchy.States.Num(), StatesWithActions));
	TestTrue(TEXT("ACTS were read for at least some states"), StatesWithActions > 0);

	// An unarmed humanoid in combat, standing. This is the case an unarmed
	// player attacking a creature actually hits.
	const int64 InCombat = (int64)1 << (int32)ESWGState::Combat;
	const FSWGAnimationState* UnarmedCombat = SWGLocomotion::ResolveState(Hierarchy, ESWGPosture::Upright, InCombat, FString());
	if (!TestNotNull(TEXT("resolves an unarmed combat state"), UnarmedCombat))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("unarmed combat state: %s"), *FString::Join(Hierarchy.PathTo(*UnarmedCombat), TEXT("/"))));

	// Which states actually author this action, so a resolution failure says
	// where the mapping does live rather than only that it wasn't found.
	for (const FSWGAnimationState& State : Hierarchy.States)
	{
		if (const FString* Mapped = State.Actions.Find(OnHit->AttackerAction))
		{
			AddInfo(FString::Printf(TEXT("  authored at %s -> '%s'"),
				*FString::Join(Hierarchy.PathTo(State), TEXT("/")), **Mapped));
		}
	}

	// The combat states keep their actions in MVAC wrappers, so a reader that
	// only takes ACTS's direct ACTN children comes back with a handful of
	// unwrapped entries and looks like it worked. Guard the shape, not just
	// the presence.
	if (const FSWGAnimationState* UnarmedCombatNode = Hierarchy.Find(TEXT("combat")))
	{
		AddInfo(FString::Printf(TEXT("root/combat has %d actions"), UnarmedCombatNode->Actions.Num()));
		TestTrue(TEXT("the unarmed combat state has its full action table, not just the unwrapped entries"),
			UnarmedCombatNode->Actions.Num() > 50);
	}

	const FString UnarmedLogical = Hierarchy.ResolveAction(*UnarmedCombat, OnHit->AttackerAction);
	AddInfo(FString::Printf(TEXT("unarmed '%s' -> '%s'"), *OnHit->AttackerAction, *UnarmedLogical));
	TestFalse(TEXT("the unarmed attack action resolves to a logical animation"), UnarmedLogical.IsEmpty());
	TestNotEqual(TEXT("and it is not the 'default' sentinel"), UnarmedLogical, FString(TEXT("default")));

	const FString UnarmedClip = SWGLocomotion::ResolveActionClip(Hierarchy, Lat, *UnarmedCombat, OnHit->AttackerAction);
	AddInfo(FString::Printf(TEXT("unarmed clip: '%s'"), *UnarmedClip));
	TestFalse(TEXT("the unarmed attack resolves all the way to a clip"), UnarmedClip.IsEmpty());
	TestTrue(TEXT("and that clip is a .ans path"), UnarmedClip.EndsWith(TEXT(".ans")));

	// The same action under a weapon subtree must resolve to a different clip
	// — that is the entire reason the .ash indirection exists.
	const FString PolearmNode = SWGLocomotion::WeaponStateNameForTemplate(TEXT("object/weapon/melee/polearm/shared_lance_vibrolance.iff"));
	TestEqual(TEXT("a polearm template selects the polearm subtree"), PolearmNode, FString(TEXT("polearm")));

	const FSWGAnimationState* PolearmCombat = SWGLocomotion::ResolveState(Hierarchy, ESWGPosture::Upright, InCombat, PolearmNode);
	if (TestNotNull(TEXT("resolves a polearm combat state"), PolearmCombat))
	{
		AddInfo(FString::Printf(TEXT("polearm combat state: %s"), *FString::Join(Hierarchy.PathTo(*PolearmCombat), TEXT("/"))));
		TestNotEqual(TEXT("the polearm state is not the unarmed one"), (const void*)PolearmCombat, (const void*)UnarmedCombat);

		const FString PolearmClip = SWGLocomotion::ResolveActionClip(Hierarchy, Lat, *PolearmCombat, OnHit->AttackerAction);
		AddInfo(FString::Printf(TEXT("polearm clip: '%s'"), *PolearmClip));
		TestFalse(TEXT("the polearm attack resolves to a clip"), PolearmClip.IsEmpty());
		TestNotEqual(TEXT("and it differs from the unarmed one"), PolearmClip, UnarmedClip);
	}

	// The defender's reaction goes through the same chain, from its own state.
	const FString ReactionClip = SWGLocomotion::ResolveActionClip(Hierarchy, Lat, *UnarmedCombat, OnHit->DefenderAction);
	AddInfo(FString::Printf(TEXT("defender reaction clip: '%s'"), *ReactionClip));
	TestFalse(TEXT("the defender's reaction resolves to a clip"), ReactionClip.IsEmpty());

	// ── the clips actually decode ─────────────────────────────────────────
	// Resolving to a path proves the lookup chain, not that anything will
	// render. Combat clips are FORM CKAT, the compressed encoding, so decode
	// one and check the quaternions came out usable rather than the
	// invalid/discontinuous values a mis-shifted bit unpack produces.
	for (const FString& ClipPath : { UnarmedClip, ReactionClip })
	{
		FSWGAnimationData Animation;
		if (!TestTrue(FString::Printf(TEXT("'%s' decodes"), *ClipPath),
			FSWGAnimationReader::ReadAnimation(Tre->CreateIffReader(ClipPath), Animation)))
		{
			continue;
		}

		int32 KeyframeCount = 0;
		int32 NonUnitCount = 0;
		for (const FSWGAnimationBoneTrack& Track : Animation.BoneTracks)
		{
			for (const TPair<int32, FQuat>& Keyframe : Track.Keyframes)
			{
				++KeyframeCount;
				// A decode that misreads the packing yields NaNs and
				// wildly off-unit quaternions; a correct one is unit-length
				// to within the 10-bit quantisation.
				if (Keyframe.Value.ContainsNaN() || FMath::Abs(Keyframe.Value.Size() - 1.0f) > 0.01f)
				{
					++NonUnitCount;
				}
			}
		}

		AddInfo(FString::Printf(TEXT("  %s: %d bones, %d frames, %d keys, %d non-unit"),
			*FPaths::GetBaseFilename(ClipPath), Animation.BoneTracks.Num(), Animation.FrameCount, KeyframeCount, NonUnitCount));

		TestTrue(TEXT("the clip animates some bones"), Animation.BoneTracks.Num() > 0);
		TestTrue(TEXT("the clip has keyframes"), KeyframeCount > 0);
		TestEqual(TEXT("every decoded rotation is a unit quaternion"), NonUnitCount, 0);
	}

	return true;
}

/**
 * The posture side of dying: Core3 clears the Combat bit and sets posture
 * Dead/Incapacitated in the same packet, and the .ash has to turn that into a
 * fall clip and a lying-down loop. all_b.ash authors "incapacitated_face_down"
 * only at the root and links to it only from the root, so this is where the
 * ancestor fallbacks in ResolveState and ResolveTransitionClip earn their keep.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGDeathPostureChainTest,
	"SWGEmu.Animation.DeathPostureChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGDeathPostureChainTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForCombatAnimationChainTest(*this);
	if (!Tre)
	{
		return false;
	}

	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	FSWGAnimationStateHierarchy Hierarchy;
	if (!TestTrue(TEXT("appearance/ash/all_b.ash parses"),
		FSWGAshReader::ReadHierarchy(Tre->CreateIffReader(TEXT("appearance/ash/all_b.ash")), Hierarchy)))
	{
		return false;
	}

	FSWGLatData Lat;
	if (!TestTrue(TEXT("appearance/lat/all_m.lat parses"),
		FSWGLatReader::ReadLat(Tre->CreateIffReader(TEXT("appearance/lat/all_m.lat")), Lat)))
	{
		return false;
	}

	const int64 InCombat = SWGStateBit(ESWGState::Combat);
	const int64 NoStates = 0;

	// Dead resolves to the lying-down node whether or not Combat is still
	// set — the node exists only at the root, and a creature killed mid-fight
	// must not keep the combat-standing loop until the state delta lands.
	const FSWGAnimationState* DeadInCombat = SWGLocomotion::ResolveState(Hierarchy, ESWGPosture::Dead, InCombat);
	const FSWGAnimationState* DeadAtPeace = SWGLocomotion::ResolveState(Hierarchy, ESWGPosture::Dead, NoStates);
	if (TestNotNull(TEXT("Dead resolves in combat"), DeadInCombat) && TestNotNull(TEXT("Dead resolves at peace"), DeadAtPeace))
	{
		AddInfo(FString::Printf(TEXT("Dead in combat: %s"), *FString::Join(Hierarchy.PathTo(*DeadInCombat), TEXT("/"))));
		TestEqual(TEXT("Dead in combat lands on the same node as Dead at peace"), (const void*)DeadInCombat, (const void*)DeadAtPeace);
		TestEqual(TEXT("and that node is incapacitated_face_down"), DeadInCombat->Name, FString(TEXT("incapacitated_face_down")));
	}

	FSWGLocomotionClipSet DeadClips;
	TestTrue(TEXT("Dead has a loop"), SWGLocomotion::ResolveClipSet(Hierarchy, Lat, ESWGPosture::Dead, InCombat, DeadClips));
	AddInfo(FString::Printf(TEXT("Dead loop: %s -> %s"), *DeadClips.IdleLoopName, *DeadClips.IdlePath));
	TestEqual(TEXT("Dead loops loop_incapacitated_face_down"), DeadClips.IdleLoopName, FString(TEXT("loop_incapacitated_face_down")));

	FSWGLocomotionClipSet IncapacitatedClips;
	TestTrue(TEXT("Incapacitated has a loop"), SWGLocomotion::ResolveClipSet(Hierarchy, Lat, ESWGPosture::Incapacitated, InCombat, IncapacitatedClips));
	TestEqual(TEXT("Incapacitated loops loop_incapacitated_face_up"), IncapacitatedClips.IdleLoopName, FString(TEXT("loop_incapacitated_face_up")));

	// The transitions, as the server sequences them: fighting upright, then
	// Combat cleared and posture changed together.
	const FString DeathClip = SWGLocomotion::ResolveTransitionClip(Hierarchy, Lat, ESWGPosture::Upright, InCombat, ESWGPosture::Dead, NoStates);
	AddInfo(FString::Printf(TEXT("death transition: '%s'"), *DeathClip));
	TestTrue(TEXT("dying from combat standing plays the fall-forward clip"), DeathClip.Contains(TEXT("get_hit_heavy_forward")));

	// The .ash names this link trn_rea_get_hit_heavy_backward, which the .lat
	// resolves to all_b_rea_get_hit_heavy_front_high.ans — a hit from the
	// front, falling onto the back — so the clip name is not the link name.
	const FString IncapacitationClip = SWGLocomotion::ResolveTransitionClip(Hierarchy, Lat, ESWGPosture::Upright, InCombat, ESWGPosture::Incapacitated, NoStates);
	AddInfo(FString::Printf(TEXT("incapacitation transition: '%s'"), *IncapacitationClip));
	TestTrue(TEXT("incapacitation from combat standing plays a fall clip"), IncapacitationClip.Contains(TEXT("get_hit_heavy")));
	TestNotEqual(TEXT("and it is not the death fall"), IncapacitationClip, DeathClip);

	// Resolving both ends with the post-change bitmask was the old bug: it
	// found no change at all when the posture delta had already been seen.
	const FString StaleClip = SWGLocomotion::ResolveTransitionClip(Hierarchy, Lat, ESWGPosture::Dead, NoStates, ESWGPosture::Dead, NoStates);
	TestTrue(TEXT("Dead to Dead is not a transition"), StaleClip.IsEmpty());

	// A corpse standing back up (revive) is authored too.
	const FString ReviveClip = SWGLocomotion::ResolveTransitionClip(Hierarchy, Lat, ESWGPosture::Incapacitated, NoStates, ESWGPosture::Upright, NoStates);
	AddInfo(FString::Printf(TEXT("revive transition: '%s'"), *ReviveClip));
	TestTrue(TEXT("getting up from incapacitated plays a clip"), ReviveClip.Contains(TEXT("incapacitated_face_up_to_standing")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
