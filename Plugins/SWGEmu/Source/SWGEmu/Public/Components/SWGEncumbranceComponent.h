#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "SWGEncumbranceComponent.generated.h"

struct FSWGPacket;

/** CREO base4 — equipment-weight-derived encumbrance, kept separate from
 *  USWGHealthComponent so Health stays a pure current-state container. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMU_API USWGEncumbranceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGEncumbranceComponent();

	TSWGBaselineList<int32> Encumbrances;
	bool bHasBase4 = false;

	void ApplyBase4(FSWGPacket& Packet);
};
