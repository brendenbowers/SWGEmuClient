#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "SWGTangibleComponent.generated.h"

struct FSWGPacket;

/**
 * TANO base3 identity/appearance fields — shared by every tangible object
 * (ASWGItem) and every creature (ASWGCreature, since CREO extends TANO on
 * the wire). Durability (ConditionDamage/MaxCondition/UseCount) and combat
 * targeting (DefenderList) are split into USWGConditionComponent and
 * USWGDefenderComponent — see world-object-plan.html "Component breakdown".
 */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMU_API USWGTangibleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGTangibleComponent();

	float          Complexity = 0.f;
	FSWGStringId   ObjectName;
	FString        CustomName;
	int32          Volume = 0;
	FString        CustomizationString;
	TSWGBaselineList<int32> VisibleComponents;
	int32          OptionsBitmask = 0;
	uint8          ObjectVisible = 0;
	bool           bHasBase3 = false;

	void ApplyBase3(FSWGPacket& Packet);
};
