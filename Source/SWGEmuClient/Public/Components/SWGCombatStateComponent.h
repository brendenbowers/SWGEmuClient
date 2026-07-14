#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWGCombatStateComponent.generated.h"

struct FSWGPacket;

/** CREO base3 (Posture/FactionRank/StateBitmask) + base6 (TargetId/WeaponId/Frozen). */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGCombatStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGCombatStateComponent();

	// Base3
	uint8 Posture      = 0;
	uint8 FactionRank  = 0;
	int64 StateBitmask = 0;
	bool bHasBase3 = false;

	// Base6
	int64 TargetId = 0;
	int64 WeaponId = 0;
	uint8 Frozen   = 0;
	bool bHasBase6 = false;

	// Split: CREO base3's Posture/FactionRank come before the CreatureLinkId/
	// Height/ShockWounds fields, StateBitmask comes after — see
	// SWGCreatureBaselineParser::ParseBase3.
	void ApplyBase3Part1(FSWGPacket& Packet); // Posture, FactionRank
	void ApplyBase3Part2(FSWGPacket& Packet); // StateBitmask

	// Split: CREO base6's WeaponId, TargetId, and Frozen are each separated by
	// other components' fields — see SWGCreatureBaselineParser::ParseBase6.
	void ApplyBase6Part1(FSWGPacket& Packet); // WeaponId
	void ApplyBase6Part2(FSWGPacket& Packet); // TargetId
	void ApplyBase6Part3(FSWGPacket& Packet); // Frozen
};
