#include "Subsystems/SWGItemTransferSubsystem.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWGItemTransfer, Log, All);

namespace
{
	UGameInstance* FindGameInstance(UWorld* World)
	{
		// The editor console hands over the editor world; the player lives in the play world.
		if (GEngine && (!World || !World->IsGameWorld()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
		}
		return World ? World->GetGameInstance() : nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GSWGEquipCommand(
	TEXT("swg.Equip"),
	TEXT("Equip the item with the given object id."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = FindGameInstance(World);
		USWGItemTransferSubsystem* Transfer = GameInstance ? GameInstance->GetSubsystem<USWGItemTransferSubsystem>() : nullptr;
		if (!Transfer || Args.Num() < 1)
		{
			UE_LOG(LogSWGItemTransfer, Warning, TEXT("usage: swg.Equip <objectId>"));
			return;
		}
		Transfer->EquipItem(FCString::Atoi64(*Args[0]));
	}));

static FAutoConsoleCommandWithWorldAndArgs GSWGUnequipCommand(
	TEXT("swg.Unequip"),
	TEXT("Unequip the item with the given object id into the inventory."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = FindGameInstance(World);
		USWGItemTransferSubsystem* Transfer = GameInstance ? GameInstance->GetSubsystem<USWGItemTransferSubsystem>() : nullptr;
		if (!Transfer || Args.Num() < 1)
		{
			UE_LOG(LogSWGItemTransfer, Warning, TEXT("usage: swg.Unequip <objectId>"));
			return;
		}
		Transfer->UnequipItem(FCString::Atoi64(*Args[0]));
	}));

void USWGItemTransferSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Commands = Collection.InitializeDependency<USWGCommandSubsystem>();
	ObjectGraph = Collection.InitializeDependency<USWGObjectGraphSubsystem>();
	MeshGenerator = Collection.InitializeDependency<USWGMeshGeneratorSubsystem>();
	Tre = Collection.InitializeDependency<USWGTreSubsystem>();
}

bool USWGItemTransferSubsystem::IsEquippable(int64 ObjectId) const
{
	const ISWGNetworkObjectInterface* NetObject = ObjectGraph ? Cast<ISWGNetworkObjectInterface>(ObjectGraph->FindActor(ObjectId)) : nullptr;
	TArray<TArray<FString>> Groups;
	return NetObject && MeshGenerator && MeshGenerator->ResolveArrangementGroups(NetObject->GetObjectCrc(), Groups);
}

bool USWGItemTransferSubsystem::IsEquipped(int64 ObjectId) const
{
	if (!ObjectGraph)
	{
		return false;
	}
	const int64* ContainerId = ObjectGraph->FindContainerId(ObjectId);
	const int32* ContainmentType = ObjectGraph->FindContainmentType(ObjectId);
	return ContainerId && ContainmentType
		&& *ContainerId == ObjectGraph->GetLocalPlayerObjectId()
		&& SWGIsSlottedArrangement(*ContainmentType);
}

int64 USWGItemTransferSubsystem::FindInventoryBagId() const
{
	if (!ObjectGraph || !Tre)
	{
		return 0;
	}
	for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(ObjectGraph->GetLocalPlayerObjectId()))
	{
		if (const ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(ObjectGraph->FindActor(ObjectId)))
		{
			if (Tre->ResolveTemplatePath(NetObject->GetObjectCrc()).Contains(TEXT("character_inventory")))
			{
				return ObjectId;
			}
		}
	}
	return 0;
}

int64 USWGItemTransferSubsystem::FindDatapadBagId() const
{
	if (!ObjectGraph || !Tre)
	{
		return 0;
	}
	for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(ObjectGraph->GetLocalPlayerObjectId()))
	{
		if (const ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(ObjectGraph->FindActor(ObjectId)))
		{
			if (Tre->ResolveTemplatePath(NetObject->GetObjectCrc()).Contains(TEXT("character_datapad")))
			{
				return ObjectId;
			}
		}
	}
	return 0;
}

bool USWGItemTransferSubsystem::EquipItem(int64 ObjectId)
{
	if (!ObjectGraph || !MeshGenerator || IsEquipped(ObjectId))
	{
		return false;
	}

	const int64 PlayerId = ObjectGraph->GetLocalPlayerObjectId();
	const ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(ObjectGraph->FindActor(ObjectId));
	TArray<TArray<FString>> Groups;
	if (PlayerId == 0 || !NetObject || !MeshGenerator->ResolveArrangementGroups(NetObject->GetObjectCrc(), Groups))
	{
		UE_LOG(LogSWGItemTransfer, Warning, TEXT("EquipItem: %lld has no arrangement — not equippable"), ObjectId);
		return false;
	}

	const TMap<FString, int64> Occupied = GatherOccupiedSlots(PlayerId);
	int32 GroupIndex = Groups.IndexOfByPredicate([&Occupied](const TArray<FString>& Slots)
	{
		return !Slots.ContainsByPredicate([&Occupied](const FString& Slot) { return Occupied.Contains(Slot); });
	});

	if (GroupIndex == INDEX_NONE)
	{
		// Every group is blocked: make room in the first one, the way retail
		// swaps a worn piece for the one being equipped.
		GroupIndex = 0;
		const int64 BagId = FindInventoryBagId();
		TSet<int64> Displaced;
		for (const FString& Slot : Groups[0])
		{
			if (const int64* Occupant = Occupied.Find(Slot); Occupant && BagId != 0 && !Displaced.Contains(*Occupant))
			{
				Displaced.Add(*Occupant);
				SendTransfer(*Occupant, BagId, static_cast<int32>(ESWGContainmentType::VolumeContained));
			}
		}
	}

	SendTransfer(ObjectId, PlayerId, static_cast<int32>(ESWGContainmentType::SlottedArrangementBase) + GroupIndex);
	return true;
}

bool USWGItemTransferSubsystem::UnequipItem(int64 ObjectId)
{
	const int64 BagId = FindInventoryBagId();
	if (BagId == 0 || !IsEquipped(ObjectId))
	{
		return false;
	}
	SendTransfer(ObjectId, BagId, static_cast<int32>(ESWGContainmentType::VolumeContained));
	return true;
}

FString USWGItemTransferSubsystem::TransferCommandFor(int64 ObjectId) const
{
	const ISWGNetworkObjectInterface* NetObject = ObjectGraph ? Cast<ISWGNetworkObjectInterface>(ObjectGraph->FindActor(ObjectId)) : nullptr;
	const FString TemplatePath = NetObject && Tre ? Tre->ResolveTemplatePath(NetObject->GetObjectCrc()) : FString();

	if (TemplatePath.Contains(TEXT("/wearables/armor/")))
	{
		return TEXT("transferitemarmor");
	}
	if (TemplatePath.StartsWith(TEXT("object/weapon/")) || TemplatePath.Contains(TEXT("/instrument/")) || TemplatePath.Contains(TEXT("/fishing/")))
	{
		return TEXT("transferitemweapon");
	}
	return TEXT("transferitemmisc");
}

TMap<FString, int64> USWGItemTransferSubsystem::GatherOccupiedSlots(int64 PlayerId) const
{
	TMap<FString, int64> Occupied;
	for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(PlayerId))
	{
		const int32* ContainmentType = ObjectGraph->FindContainmentType(ObjectId);
		const ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(ObjectGraph->FindActor(ObjectId));
		TArray<FString> SlotNames;
		if (ContainmentType && NetObject && MeshGenerator->ResolveArrangementSlotNames(NetObject->GetObjectCrc(), *ContainmentType, SlotNames))
		{
			for (const FString& Slot : SlotNames)
			{
				Occupied.Add(Slot, ObjectId);
			}
		}
	}
	return Occupied;
}

void USWGItemTransferSubsystem::SendTransfer(int64 ObjectId, int64 DestinationId, int32 ContainmentType)
{
	if (!Commands)
	{
		return;
	}
	// "<destination> <containmentType> <x> <y> <z>" — the position only matters for drops into a cell.
	const FString Arguments = FString::Printf(TEXT("%lld %d 0.000000 0.000000 0.000000"), DestinationId, ContainmentType);
	const FString Command = TransferCommandFor(ObjectId);
	Commands->SendCommand(Command, ObjectId, Arguments);
	UE_LOG(LogSWGItemTransfer, Log, TEXT("%s %lld -> %lld (containmentType %d)"), *Command, ObjectId, DestinationId, ContainmentType);
}
