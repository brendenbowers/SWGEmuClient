#include "SWGInventoryQuery.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Components/SWGTangibleComponent.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Objects/Tangible/SWGItem.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"
#include "Engine/GameInstance.h"

namespace
{
	/** The player's inventory bag — the volume container under the creature whose template is character_inventory. */
	int64 FindInventoryBagId(USWGObjectGraphSubsystem* ObjectGraph, USWGTreSubsystem* Tre, int64 PlayerId)
	{
		if (!Tre)
		{
			return 0;
		}
		for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(PlayerId))
		{
			if (ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(ObjectGraph->FindActor(ObjectId)))
			{
				if (Tre->ResolveTemplatePath(NetObject->GetObjectCrc()).Contains(TEXT("character_inventory")))
				{
					return ObjectId;
				}
			}
		}
		return 0;
	}
}

FSWGInventoryEntry SWGInventoryQuery::Describe(UGameInstance* GameInstance, int64 ObjectId)
{
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	AActor* Actor = ObjectGraph ? ObjectGraph->FindActor(ObjectId) : nullptr;

	FSWGInventoryEntry Entry;
	Entry.ObjectId = ObjectId;

	if (const USWGTangibleComponent* Tangible = Actor ? Actor->FindComponentByClass<USWGTangibleComponent>() : nullptr)
	{
		Entry.Name = Tangible->GetDisplayName();
	}

	if (const ASWGItem* Item = Cast<ASWGItem>(Actor); Item && Item->ResourceQuantity > 0)
	{
		Entry.Quantity = Item->ResourceQuantity;
		if (!Item->ResourceName.IsEmpty())
		{
			Entry.Name = Item->ResourceName;
		}
	}

	if (Entry.Name.IsEmpty())
	{
		// Baselines still in flight, or a template with no name at all.
		Entry.Name = FString::Printf(TEXT("object %lld"), ObjectId);
	}
	return Entry;
}

bool SWGInventoryQuery::Gather(UGameInstance* GameInstance, TArray<FSWGInventoryEntry>& Equipped, TArray<FSWGInventoryEntry>& Contents)
{
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	const int64 PlayerId = ObjectGraph ? ObjectGraph->GetLocalPlayerObjectId() : 0;

	TArray<FSWGInventoryEntry> NewEquipped;
	TArray<FSWGInventoryEntry> NewContents;

	if (PlayerId != 0)
	{
		for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(PlayerId))
		{
			const int32* ContainmentType = ObjectGraph->FindContainmentType(ObjectId);
			if (!ContainmentType || !SWGIsSlottedArrangement(*ContainmentType))
			{
				continue;
			}

			FSWGInventoryEntry Entry = Describe(GameInstance, ObjectId);

			// The bags (inventory, datapad, bank, mission_bag) are "equipped"
			// on the wire too, but sit in slots that never show on the body —
			// those aren't gear.
			AActor* Actor = ObjectGraph->FindActor(ObjectId);
			ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(Actor);
			TArray<FString> SlotNames;
			if (NetObject && MeshGenerator && MeshGenerator->ResolveArrangementSlotNames(NetObject->GetObjectCrc(), *ContainmentType, SlotNames))
			{
				if (!MeshGenerator->IsAnySlotAppearanceRelated(SlotNames))
				{
					continue;
				}
				Entry.SlotNames = FString::Join(SlotNames, TEXT(", "));
			}

			NewEquipped.Add(MoveTemp(Entry));
		}

		if (const int64 BagId = FindInventoryBagId(ObjectGraph, Tre, PlayerId))
		{
			for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(BagId))
			{
				NewContents.Add(Describe(GameInstance, ObjectId));
			}
		}
	}

	NewEquipped.Sort([](const FSWGInventoryEntry& Left, const FSWGInventoryEntry& Right) { return Left.SlotNames < Right.SlotNames; });
	NewContents.Sort([](const FSWGInventoryEntry& Left, const FSWGInventoryEntry& Right) { return Left.Name < Right.Name; });

	if (NewEquipped == Equipped && NewContents == Contents)
	{
		return false;
	}

	Equipped = MoveTemp(NewEquipped);
	Contents = MoveTemp(NewContents);
	return true;
}
