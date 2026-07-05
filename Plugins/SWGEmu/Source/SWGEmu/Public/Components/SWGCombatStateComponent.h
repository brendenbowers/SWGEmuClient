#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWGCombatStateComponent.generated.h"

struct FSWGPacket;

/** CREO base3 (Posture/FactionRank/StateBitmask) + base6 (TargetId/WeaponId/Frozen). */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMU_API USWGCombatStateComponent : public UActorComponent
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

	void ApplyBase3(FSWGPacket& Packet);
	void ApplyBase6(FSWGPacket& Packet);
};
