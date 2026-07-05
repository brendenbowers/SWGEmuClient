#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Network/Objects/Zone/Creature/GroupMissionCriticalObject.h"
#include "SWGSpaceMissionComponent.generated.h"

struct FSWGPacket;

/** CREO base4 — space-combat-only fields, split out so ground NPCs don't carry them. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMU_API USWGSpaceMissionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGSpaceMissionComponent();

	int64 ListenId = 0;
	TSWGBaselineList<FGroupMissionCriticalObject> SpaceMissionObjects;
	bool bHasBase4 = false;

	void ApplyBase4(FSWGPacket& Packet);
};
