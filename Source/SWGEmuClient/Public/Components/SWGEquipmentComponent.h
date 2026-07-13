#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Network/Objects/Zone/Creature/EquiptmentItem.h"
#include "SWGEquipmentComponent.generated.h"

struct FSWGPacket;

/** CREO base6 — equipped items + composite appearance override. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGEquipmentComponent();

	TSWGBaselineList<FEquiptmentItem> EquipmentList;
	FString AlternateAppearance;
	bool bHasBase6 = false;

	void ApplyBase6(FSWGPacket& Packet);
};
