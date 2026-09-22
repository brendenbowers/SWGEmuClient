#include "SWGDatapadQuery.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGItemTransferSubsystem.h"
#include "Subsystems/SWGIntangibleObjectSubsystem.h"
#include "Subsystems/SWGMissionSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Engine/GameInstance.h"

bool SWGDatapadQuery::Gather(UGameInstance* GameInstance, TArray<FSWGInventoryEntry>& Contents)
{
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	USWGItemTransferSubsystem* Transfer = GameInstance ? GameInstance->GetSubsystem<USWGItemTransferSubsystem>() : nullptr;
	USWGIntangibleObjectSubsystem* Intangibles = GameInstance ? GameInstance->GetSubsystem<USWGIntangibleObjectSubsystem>() : nullptr;
	USWGMissionSubsystem* Missions = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr;
	USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;

	TArray<FSWGInventoryEntry> NewContents;

	if (ObjectGraph && Transfer)
	{
		if (const int64 BagId = Transfer->FindDatapadBagId())
		{
			for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(BagId))
			{
				if (const FSWGMissionEntry* Mission = Missions ? Missions->FindMission(ObjectId) : nullptr; Mission && !Mission->Title.IsEmpty())
				{
					FSWGInventoryEntry NewEntry;
					NewEntry.ObjectId = ObjectId;
					NewEntry.Name = Mission->Title.ToString();
					NewContents.Add(MoveTemp(NewEntry));
				}
				else if (Mission && !Mission->TargetTemplateName.IsEmpty())
				{
					FSWGInventoryEntry NewEntry;
					NewEntry.ObjectId = ObjectId;
					NewEntry.Name = Mission->TargetTemplateName.ToString();
					NewContents.Add(MoveTemp(NewEntry));
				}
				else if (const FSWGIntangibleEntry* Entry = Intangibles ? Intangibles->FindEntry(ObjectId) : nullptr; Entry && !Entry->Name.IsEmpty())
				{
					FSWGInventoryEntry NewEntry;
					NewEntry.ObjectId = ObjectId;
					NewEntry.Name = Entry->Name;
					NewContents.Add(MoveTemp(NewEntry));
				}
				else if (const uint32 Crc = ObjectGraph->FindObjectCrc(ObjectId); Tre && Crc != 0)
				{
					const FString TemplateName = Tre->ResolveTemplateObjectName(Crc);
					if (!TemplateName.IsEmpty())
					{
						FSWGInventoryEntry NewEntry;
						NewEntry.ObjectId = ObjectId;
						NewEntry.Name = TemplateName;
						NewContents.Add(MoveTemp(NewEntry));
					}
					else
					{
						NewContents.Add(SWGInventoryQuery::Describe(GameInstance, ObjectId));
					}
				}
				else
				{
					NewContents.Add(SWGInventoryQuery::Describe(GameInstance, ObjectId));
				}
			}
		}
	}

	NewContents.Sort([](const FSWGInventoryEntry& Left, const FSWGInventoryEntry& Right) { return Left.Name < Right.Name; });

	if (NewContents == Contents)
	{
		return false;
	}

	Contents = MoveTemp(NewContents);
	return true;
}
