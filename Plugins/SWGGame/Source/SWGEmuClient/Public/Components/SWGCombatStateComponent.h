#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Common/SWGPostureTypes.h"
#include "Network/Objects/Zone/Creature/CreatureObjectBaseline.h"
#include "Network/Objects/Zone/Creature/CreatureObjectDelta.h"
#include "SWGCombatStateComponent.generated.h"

struct FSWGPacket;

/** CREO base3 (Posture/FactionRank/StateBitmask) + base6 (TargetId/WeaponId/Frozen). */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGCombatStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGCombatStateComponent();

	/**
	 * Fired whenever Posture or StateBitmask actually changes value (not on
	 * every baseline/delta that merely restates them). Both the movement
	 * component's speed shaping and the skeletal animation pipeline's
	 * posture-driven clip swap hang off this, so neither has to poll.
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FSWGPostureStateChanged, ESWGPosture /*Posture*/, int64 /*StateBitmask*/);
	FSWGPostureStateChanged OnPostureOrStateChanged;

	// Base3
	//
	// Reproducing a posture-specific animation bug locally means forcing these
	// without waiting for the server to send a CREO delta — use the
	// "swg.SetPosture" console command (registered in
	// USWGMeshGeneratorSubsystem::Initialize) rather than making these
	// UPROPERTY, since adding reflection here is a UHT change and so can never
	// be hot-reloaded.
	uint8 Posture      = 0;
	uint8 FactionRank  = 0;
	int64 StateBitmask = 0;
	bool bHasBase3 = false;

	// Base6
	int64 TargetId = 0;
	int64 WeaponId = 0;
	uint8 Frozen   = 0;
	bool bHasBase6 = false;

	ESWGPosture GetPosture() const { return (ESWGPosture)Posture; }
	bool HasState(ESWGState State) const { return SWGHasState(StateBitmask, State); }

	// There is no separate "dead" flag on the wire: a corpse is the same CREO
	// with its posture set to Dead, until the server destroys the object. The
	// posture is the corpse marker.
	bool IsIncapacitated() const { return GetPosture() == ESWGPosture::Incapacitated; }
	bool IsDead() const { return GetPosture() == ESWGPosture::Dead; }
	/** Incapacitated or dead — the two postures nothing (attacks, movement) gets a creature out of by itself. */
	bool IsDowned() const { return IsIncapacitated() || IsDead(); }

	/**
	 * ObjController PostureUpdate (0x131) — the server's "animate this posture
	 * change now". Its delta3 twin usually lands in the same packet, so this
	 * is mostly a no-op after the diff; it matters when the delta is delayed
	 * or never sent (see FPostureUpdateIn).
	 */
	void ApplyPostureUpdate(uint8 NewPosture);

	// Split: CREO base3's Posture/FactionRank come before the CreatureLinkId/
	// Height/ShockWounds fields, StateBitmask comes after — see
	// SWGCreatureBaselineParser::ParseBase3.
	void ApplyBase3(const FCreatureObjectBaseline& Baseline);
	void ApplyDelta3(const FCreatureObjectDelta& Delta);

	// Split: CREO base6's WeaponId, TargetId, and Frozen are each separated by
	// other components' fields — see SWGCreatureBaselineParser::ParseBase6.
	void ApplyBase6(const FCreatureObjectBaseline& Baseline);
	void ApplyDelta6(const FCreatureObjectDelta& Delta);

private:
	/** Broadcasts OnPostureOrStateChanged if Posture/StateBitmask differ from the values captured before the apply. */
	void BroadcastIfPostureOrStateChanged(uint8 PreviousPosture, int64 PreviousStateBitmask);
};
