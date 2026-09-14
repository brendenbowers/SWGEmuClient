#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Player/PlayerObjectBaseline.h"
#include "Network/Objects/Zone/Player/PlayerObjectDelta.h"
#include "SWGJournalComponent.generated.h"

/**
 * PLAY base8 — the datapad's journal: waypoints and quest state. Completed and
 * active quests are bitfields, eight quests per byte.
 */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGJournalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGJournalComponent();

	TSWGBaselineList<FWaypoint>         WaypointList;
	TSWGBaselineList<uint8>             CompletedQuests;
	TSWGBaselineList<uint8>             ActiveQuests;
	TSWGBaselineList<FQuestJournalItem> Quests;
	bool bHasBase8 = false;

	bool IsQuestCompleted(int32 QuestIndex) const;
	bool IsQuestActive(int32 QuestIndex) const;

	void ApplyBase8(const FPlayerObjectBaseline& Baseline);
	void ApplyDelta8(const FPlayerObjectDelta& Delta);
};
