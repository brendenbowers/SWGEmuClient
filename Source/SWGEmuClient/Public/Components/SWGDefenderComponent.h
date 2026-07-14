#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "SWGDefenderComponent.generated.h"

struct FSWGPacket;

/** TANO base6 — pure combat-target-tracking, split from USWGTangibleComponent.
 *  Confirmed to generate no scalar deltas (TangibleObjectDeltaMessage6 is empty). */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGDefenderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGDefenderComponent();

	TSWGBaselineList<uint64> DefenderList;
	bool bHasBase6 = false;

	void ApplyBase6(FSWGPacket& Packet);
};
