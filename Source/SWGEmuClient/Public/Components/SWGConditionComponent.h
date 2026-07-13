#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWGConditionComponent.generated.h"

struct FSWGPacket;

/** TANO base3 durability fields — split from USWGTangibleComponent since repair/combat
 *  systems care about condition without needing naming/appearance data. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGConditionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGConditionComponent();

	int32 UseCount        = 0;
	int32 ConditionDamage = 0;
	int32 MaxCondition    = 0;
	bool  bHasBase3       = false;

	void ApplyBase3(FSWGPacket& Packet);
};
