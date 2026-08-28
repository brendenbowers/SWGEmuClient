#include "Components/SWGJournalComponent.h"

namespace
{
	bool TestQuestBit(const TSWGBaselineList<uint8>& Bits, int32 QuestIndex)
	{
		const int32 ByteIndex = QuestIndex / 8;
		return Bits.Items.IsValidIndex(ByteIndex) && (Bits.Items[ByteIndex] & (1 << (QuestIndex % 8))) != 0;
	}
}

USWGJournalComponent::USWGJournalComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool USWGJournalComponent::IsQuestCompleted(int32 QuestIndex) const
{
	return TestQuestBit(CompletedQuests, QuestIndex);
}

bool USWGJournalComponent::IsQuestActive(int32 QuestIndex) const
{
	return TestQuestBit(ActiveQuests, QuestIndex);
}

void USWGJournalComponent::ApplyBase8(const FPlayerObjectBaseline& Baseline)
{
	WaypointList = Baseline.WaypointList;
	CompletedQuests = Baseline.CompletedQuests;
	ActiveQuests = Baseline.ActiveQuests;
	Quests = Baseline.Quests;
	bHasBase8 = true;
}

void USWGJournalComponent::ApplyDelta8(const FPlayerObjectDelta& Delta)
{
	ApplyKeyedListChanges(Delta.WaypointList, WaypointList, [](const FWaypoint& A, const FWaypoint& B)
	{
		return A.WaypointObjectId == B.WaypointObjectId;
	});

	ApplyIndexedListChanges(Delta.CompletedQuests, CompletedQuests);
	ApplyIndexedListChanges(Delta.ActiveQuests, ActiveQuests);

	ApplyKeyedListChanges(Delta.Quests, Quests, [](const FQuestJournalItem& A, const FQuestJournalItem& B)
	{
		return A.QuestCRC == B.QuestCRC;
	});
}
