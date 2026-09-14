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
