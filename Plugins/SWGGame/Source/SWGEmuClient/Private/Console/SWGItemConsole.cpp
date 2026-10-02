#include "Subsystems/SWGItemTransferSubsystem.h"
#include "Subsystems/SWGItemIconSubsystem.h"
#include "Subsystems/SWGIntangibleObjectSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWGItemTransfer, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogSWGDatapadDump, Log, All);

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

// swg.DumpDatapad — logs exactly what the client knows about the local
// player's datapad bag and each of its contents (object id, crc, resolved
// template path, whether an actor spawned, whether an ITNO baseline/name
// has arrived) — a one-shot alternative to grepping the raw session log.
static FAutoConsoleCommandWithWorldAndArgs GSWGDumpDatapadCommand(
	TEXT("swg.DumpDatapad"),
	TEXT("Logs the local player's datapad bag id and every contained object's id/crc/template/name state."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
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

		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
		USWGItemTransferSubsystem* Transfer = GameInstance ? GameInstance->GetSubsystem<USWGItemTransferSubsystem>() : nullptr;
		USWGIntangibleObjectSubsystem* Intangibles = GameInstance ? GameInstance->GetSubsystem<USWGIntangibleObjectSubsystem>() : nullptr;
		USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
		if (!ObjectGraph || !Transfer)
		{
			UE_LOG(LogSWGDatapadDump, Warning, TEXT("swg.DumpDatapad: no live session yet"));
			return;
		}

		const int64 BagId = Transfer->FindDatapadBagId();
		UE_LOG(LogSWGDatapadDump, Log, TEXT("swg.DumpDatapad: bag id = %lld"), BagId);
		if (BagId == 0)
		{
			return;
		}

		const TArray<int64> Contents = ObjectGraph->FindContainedObjectIds(BagId);
		UE_LOG(LogSWGDatapadDump, Log, TEXT("swg.DumpDatapad: %d content object(s)"), Contents.Num());
		for (const int64 ObjectId : Contents)
		{
			AActor* Actor = ObjectGraph->FindActor(ObjectId);
			const uint32 Crc = ObjectGraph->FindObjectCrc(ObjectId);
			const FString TemplatePath = Tre && Crc != 0 ? Tre->ResolveTemplatePath(Crc) : FString();
			const FSWGIntangibleEntry* Entry = Intangibles ? Intangibles->FindEntry(ObjectId) : nullptr;

			UE_LOG(LogSWGDatapadDump, Log, TEXT("  %lld: crc=%08X template='%s' actor=%s netObj=%s intangibleEntry=%s name='%s'"),
				ObjectId, Crc, *TemplatePath,
				Actor ? *Actor->GetClass()->GetName() : TEXT("<none>"),
				(Actor && Cast<ISWGNetworkObjectInterface>(Actor)) ? TEXT("yes") : TEXT("no"),
				Entry ? TEXT("yes") : TEXT("no"),
				Entry ? *Entry->Name : TEXT(""));
		}
	}));

// Not the WithWorld variant: under PIE that hands over the editor world,
// which has no icon subsystem.
FAutoConsoleCommand CmdClearItemIcons(
	TEXT("swg.ItemIcons.Clear"),
	TEXT("Forget every cached item icon so the next inventory refresh captures them again."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (USWGItemIconSubsystem* Icons = Context.World() ? Context.World()->GetSubsystem<USWGItemIconSubsystem>() : nullptr)
			{
				Icons->ClearCache();
			}
		}
	}));
