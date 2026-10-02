#include "SWGInventoryQuery.h"
#include "Objects/Tangible/SWGItem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "TRE/SWGResourceClassRow.h"
#include "HAL/IConsoleManager.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

static FAutoConsoleCommand GCraftDumpAncestryCommand(
	TEXT("swg.Craft.DumpAncestry"),
	TEXT("swg.Craft.DumpAncestry <resourceClassId> — walks SWGResourceClass::DataTablePath's ParentClass chain from the given id and logs it, to sanity-check resource-filter matching."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("usage: swg.Craft.DumpAncestry <resourceClassId>"));
			return;
		}
		const UDataTable* Table = LoadObject<UDataTable>(nullptr, *SWGResourceClass::DataTablePath);
		if (!Table)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Craft.DumpAncestry: could not load %s"), *SWGResourceClass::DataTablePath);
			return;
		}
		FString Current = Args[0];
		FString Chain = Current;
		for (int32 Depth = 0; Depth < 16; ++Depth)
		{
			const FSWGResourceClassRow* Row = Table->FindRow<FSWGResourceClassRow>(FName(*Current), TEXT("Craft"), false);
			if (!Row || Row->ParentClass.IsEmpty())
			{
				break;
			}
			Chain += TEXT(" -> ") + Row->ParentClass;
			Current = Row->ParentClass;
		}
		UE_LOG(LogTemp, Log, TEXT("swg.Craft.DumpAncestry: %s"), *Chain);
	}));

static FAutoConsoleCommand GCraftDumpInventoryCommand(
	TEXT("swg.Craft.DumpInventory"),
	TEXT("Logs the local player's bag contents (object id, name, quantity) to LogTemp, for picking an id to test swg.Craft.Add with."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UGameInstance* GameInstance = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
			{
				GameInstance = Context.World()->GetGameInstance();
				break;
			}
		}
		USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
		TArray<FSWGInventoryEntry> Equipped;
		TArray<FSWGInventoryEntry> Contents;
		SWGInventoryQuery::Gather(GameInstance, Equipped, Contents);
		UE_LOG(LogTemp, Log, TEXT("swg.Craft.DumpInventory: %d item(s) in bag"), Contents.Num());
		for (const FSWGInventoryEntry& Entry : Contents)
		{
			const ASWGItem* Item = ObjectGraph ? Cast<ASWGItem>(ObjectGraph->FindActor(Entry.ObjectId)) : nullptr;
			UE_LOG(LogTemp, Log, TEXT("  id=%lld name=%s qty=%d resourceType=%s"), Entry.ObjectId, *Entry.Name, Entry.Quantity,
				Item ? *Item->ResourceType : TEXT("(no actor)"));
		}
	}));
