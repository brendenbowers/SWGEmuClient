#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "SWGHealthComponent.generated.h"

struct FSWGPacket;

/** The HAM system — CREO base1 (BaseHAM), base3 (Wounds/ShockWounds), base6 (HAM/MaxHAM). */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGHealthComponent();

	// Base1
	TSWGBaselineList<int32> BaseHAM; // 9 pools: health/str/con/action/qck/sta/mind/foc/wil
	bool bHasBase1 = false;

	// Base3
	int32 ShockWounds = 0;
	TSWGBaselineList<int32> Wounds;
	bool bHasBase3 = false;

	// Base6
	TSWGBaselineList<int32> HAM;
	TSWGBaselineList<int32> MaxHAM;
	bool bHasBase6 = false;

	void ApplyBase1(FSWGPacket& Packet);
	// Split in two: CREO base3 interleaves ShockWounds and Wounds around
	// USWGCombatStateComponent's StateBitmask — see
	// SWGCreatureBaselineParser::ParseBase3.
	void ApplyBase3Part1(FSWGPacket& Packet); // ShockWounds
	void ApplyBase3Part2(FSWGPacket& Packet); // Wounds
	void ApplyBase6(FSWGPacket& Packet);
};
