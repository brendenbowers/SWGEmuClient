#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Network/Objects/Zone/Creature/GroupMissionCriticalObject.h"
#include "SWGSpaceMissionComponent.generated.h"

struct FSWGPacket;

/** CREO base4 — space-combat-only fields, split out so ground NPCs don't carry them. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGSpaceMissionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGSpaceMissionComponent();

	int64 ListenId = 0;
	TSWGBaselineList<FGroupMissionCriticalObject> SpaceMissionObjects;
	bool bHasBase4 = false;

	// Split in two: ListenId and SpaceMissionObjects are not adjacent on the wire
	// — USWGMovementComponent's speed fields sit between them — see
	// SWGCreatureBaselineParser::ParseBase4.
	void ApplyBase4Part1(FSWGPacket& Packet); // ListenId
	void ApplyBase4Part2(FSWGPacket& Packet); // SpaceMissionObjects
};
