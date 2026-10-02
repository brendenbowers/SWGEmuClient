#include "Subsystems/SWGCraftingSubsystem.h"
#include "SWGLogCategories.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// Developer commands for driving a crafting session without the UI (swg.Craft.*).


namespace
{
	/** Shared by every swg.Craft.* dev command: finds the running PIE/game world's crafting subsystem. */
	USWGCraftingSubsystem* ResolveCraftingSubsystem(UWorld* World)
	{
		if (GEngine && (!World || !World->GetGameInstance()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					World = Context.World();
					break;
				}
			}
		}
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGCraftingSubsystem>() : nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GCraftingStartCommand(
	TEXT("swg.Craft.Start"),
	TEXT("swg.Craft.Start <toolObjectId> — sends requestCraftingSession at a known tool/station id, bypassing the radial Use."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGCraftingSubsystem* Crafting = ResolveCraftingSubsystem(World);
		if (!Crafting || Args.IsEmpty())
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("usage: swg.Craft.Start <toolObjectId>"));
			return;
		}
		const int64 ToolObjectId = FCString::Atoi64(*Args[0]);
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Start: %lld -> %d"), ToolObjectId, Crafting->StartSession(ToolObjectId));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCraftingSelectCommand(
	TEXT("swg.Craft.Select"),
	TEXT("swg.Craft.Select <schematicIndex> — SelectSchematic on the open session."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGCraftingSubsystem* Crafting = ResolveCraftingSubsystem(World);
		if (!Crafting || Args.IsEmpty())
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("usage: swg.Craft.Select <schematicIndex>"));
			return;
		}
		const int32 Index = FCString::Atoi(*Args[0]);
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Select: %d -> %d"), Index, Crafting->SelectSchematic(Index));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCraftingAddCommand(
	TEXT("swg.Craft.Add"),
	TEXT("swg.Craft.Add <ingredientObjectId> <slot> — AddIngredient on the open session."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGCraftingSubsystem* Crafting = ResolveCraftingSubsystem(World);
		if (!Crafting || Args.Num() < 2)
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("usage: swg.Craft.Add <ingredientObjectId> <slot>"));
			return;
		}
		const int64 IngredientObjectId = FCString::Atoi64(*Args[0]);
		const int32 Slot = FCString::Atoi(*Args[1]);
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Add: %lld -> slot %d: %d"), IngredientObjectId, Slot, Crafting->AddIngredient(IngredientObjectId, Slot));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCraftingCustomizeCommand(
	TEXT("swg.Craft.Customize"),
	TEXT("swg.Craft.Customize [name] — Customize on the open session (empty name, no template/palette change)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGCraftingSubsystem* Crafting = ResolveCraftingSubsystem(World);
		if (!Crafting)
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("swg.Craft.Customize: no crafting subsystem"));
			return;
		}
		const FString Name = Args.IsEmpty() ? FString() : Args[0];
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Customize: %d"), Crafting->Customize(Name, 0xFF, 1, {}));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCraftingAssembleCommand(
	TEXT("swg.Craft.Assemble"),
	TEXT("swg.Craft.Assemble — Assemble on the open session."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGCraftingSubsystem* Crafting = ResolveCraftingSubsystem(World);
		if (!Crafting)
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("swg.Craft.Assemble: no crafting subsystem"));
			return;
		}
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Assemble: %d"), Crafting->Assemble());
	}));

static FAutoConsoleCommandWithWorldAndArgs GCraftingExperimentCommand(
	TEXT("swg.Craft.Experiment"),
	TEXT("swg.Craft.Experiment <row> <points> — Experiment on the open session."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGCraftingSubsystem* Crafting = ResolveCraftingSubsystem(World);
		if (!Crafting || Args.Num() < 2)
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("usage: swg.Craft.Experiment <row> <points>"));
			return;
		}
		const int32 Row = FCString::Atoi(*Args[0]);
		const int32 Points = FCString::Atoi(*Args[1]);
		if (!Crafting->GetExperimentGroups().IsValidIndex(Row) || Points < 1 || Points > Crafting->GetExperimentPointsRemaining())
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("swg.Craft.Experiment: invalid row or points"));
			return;
		}
		FSWGCraftingExperimentRow Attempt;
		Attempt.RowIndex = Row;
		Attempt.Points = Points;
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Experiment: row %d, points %d -> %d"), Row, Points, Crafting->Experiment({Attempt}));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCraftingCreateCommand(
	TEXT("swg.Craft.Create"),
	TEXT("swg.Craft.Create [practice] — CreatePrototype on the open session; \"practice\" skips the item."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGCraftingSubsystem* Crafting = ResolveCraftingSubsystem(World);
		if (!Crafting)
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("swg.Craft.Create: no crafting subsystem"));
			return;
		}
		const bool bPractice = !Args.IsEmpty() && Args[0].Equals(TEXT("practice"), ESearchCase::IgnoreCase);
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Create: practice=%d -> %d"), bPractice, Crafting->CreatePrototype(bPractice));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCraftingRetrieveCommand(
	TEXT("swg.Craft.Retrieve"),
	TEXT("swg.Craft.Retrieve <toolObjectId> — sends the SERVER_ITEM_OPTIONS \"Retrieve output\" radial select for the given tool."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGCraftingSubsystem* Crafting = ResolveCraftingSubsystem(World);
		if (!Crafting)
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("swg.Craft.Retrieve: no crafting subsystem"));
			return;
		}
		if (Args.IsEmpty())
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("swg.Craft.Retrieve: usage: swg.Craft.Retrieve <toolObjectId>"));
			return;
		}
		const int64 ToolObjId = FCString::Atoi64(*Args[0]);
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Retrieve: %lld -> %d"), ToolObjId, Crafting->RetrieveOutput(ToolObjId));
	}));

static FAutoConsoleCommandWithWorldAndArgs GCraftingDumpSlotsCommand(
	TEXT("swg.Craft.DumpSlots"),
	TEXT("Logs the open session's assembly slots (index, name, filled/required, object ids) to LogSWGCrafting."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGCraftingSubsystem* Crafting = ResolveCraftingSubsystem(World);
		if (!Crafting)
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("swg.Craft.DumpSlots: no crafting subsystem"));
			return;
		}
		const TArray<FSWGCraftingSlot>& Slots = Crafting->GetSlots();
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.DumpSlots: %d slot(s), ready=%d"), Slots.Num(), Crafting->IsAssemblyReady());
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			const FSWGCraftingSlot& Slot = Slots[Index];
			FString Ids;
			for (const int64 ObjectId : Slot.FilledObjectIds) { Ids += FString::Printf(TEXT("%lld "), ObjectId); }
			UE_LOG(LogSWGCrafting, Log, TEXT("  [%d] %s (%d/%d) optional=%d kind=%d resourceType=%s ids=%s"), Index, *Slot.Name, Slot.FilledQuantity, Slot.RequiredQuantity, Slot.bOptional, static_cast<int32>(Slot.Kind), *Slot.ResourceType, *Ids);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GCraftingDumpCommand(
	TEXT("swg.Craft.Dump"),
	TEXT("Logs the current session's schematic list (crc, tab, name) to LogSWGCrafting, for picking a CRC to test with."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (GEngine && (!World || !World->GetGameInstance()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					World = Context.World();
					break;
				}
			}
		}
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGCraftingSubsystem* Crafting = GameInstance ? GameInstance->GetSubsystem<USWGCraftingSubsystem>() : nullptr;
		if (!Crafting)
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("swg.Craft.Dump: no crafting subsystem"));
			return;
		}
		const TArray<FSWGCraftingSchematicOption>& Schematics = Crafting->GetSchematics();
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Dump: %d schematic(s)"), Schematics.Num());
		for (const FSWGCraftingSchematicOption& Option : Schematics)
		{
			UE_LOG(LogSWGCrafting, Log, TEXT("  crc=0x%08X tab=%d name=%s"), static_cast<uint32>(Option.SchematicCrc), Option.ToolTab, *Option.Name);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GCraftingFakeCommand(
	TEXT("swg.Craft.Fake"),
	TEXT("Fakes a crafting session for UI work without a tool: no args = schematic list, \"assembly\" = Assembling stage (slots, no experiment rows), \"slots\" = Experimenting stage (slots+experiment rows)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (GEngine && (!World || !World->GetGameInstance()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					World = Context.World();
					break;
				}
			}
		}
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGCraftingSubsystem* Crafting = GameInstance ? GameInstance->GetSubsystem<USWGCraftingSubsystem>() : nullptr;
		if (!Crafting)
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("swg.Craft.Fake: no world/game instance/subsystem found"));
			return;
		}
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Fake: subsystem found, injecting"));
		if (!Args.IsEmpty() && Args[0].Equals(TEXT("assembly"), ESearchCase::IgnoreCase))
		{
			TArray<FSWGCraftingSlot> Slots;
			{
				FSWGCraftingSlot& Slot = Slots.AddDefaulted_GetRef();
				Slot.Name = TEXT("dried fruit"); Slot.ResourceType = TEXT("organic"); Slot.Kind = ESWGDraftSlotKind::Resource; Slot.RequiredQuantity = 10; Slot.FilledQuantity = 4;
			}
			{
				FSWGCraftingSlot& Slot = Slots.AddDefaulted_GetRef();
				Slot.Name = TEXT("water"); Slot.ResourceType = TEXT("water"); Slot.Kind = ESWGDraftSlotKind::Resource; Slot.RequiredQuantity = 5; Slot.FilledQuantity = 5;
			}
			{
				// Mixed (not resource-filtered) so the floor row has something
				// to show regardless of what real resources the test inventory holds.
				FSWGCraftingSlot& Slot = Slots.AddDefaulted_GetRef();
				Slot.Name = TEXT("container"); Slot.Kind = ESWGDraftSlotKind::Mixed; Slot.bOptional = true; Slot.RequiredQuantity = 1; Slot.FilledQuantity = 0;
			}
			Crafting->InjectAssemblySlots(Slots);
			UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Fake assembly: injected %d slots, state=%d"), Crafting->GetSlots().Num(), static_cast<int32>(Crafting->GetState()));
			return;
		}
		if (!Args.IsEmpty() && !Args[0].Equals(TEXT("slots"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogSWGCrafting, Warning, TEXT("swg.Craft.Fake: expected assembly or slots"));
			return;
		}
		if (Args.IsEmpty())
		{
			TArray<FSWGCraftingSchematicOption> Fake;
			const TCHAR* const Names[] = { TEXT("dried fruit"), TEXT("survey tool"), TEXT("padded vest") };
			const int32 Tabs[] = { 4, 3, 2 };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				FSWGCraftingSchematicOption& Option = Fake.AddDefaulted_GetRef();
				Option.SchematicCrc = 0x1000 + Index;
				Option.ToolTab = Tabs[Index];
				Option.Name = Names[Index];
			}
			Crafting->InjectSchematics(Fake);
			UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Fake: injected %d schematics, state=%d"), Crafting->GetSchematics().Num(), static_cast<int32>(Crafting->GetState()));
			return;
		}
		TArray<FSWGCraftingSlot> Slots;
		{
			FSWGCraftingSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Name = TEXT("dried fruit"); Slot.ResourceType = TEXT("organic"); Slot.Kind = ESWGDraftSlotKind::Resource; Slot.RequiredQuantity = 10; Slot.FilledQuantity = 4;
		}
		{
			FSWGCraftingSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Name = TEXT("water"); Slot.ResourceType = TEXT("water"); Slot.Kind = ESWGDraftSlotKind::Resource; Slot.RequiredQuantity = 5; Slot.FilledQuantity = 5;
		}
		TArray<FSWGCraftingExperimentGroup> Groups;
		{
			FSWGCraftingExperimentGroup& Group = Groups.AddDefaulted_GetRef();
			Group.Title = TEXT("exp_quality"); Group.CurrentPercent = 0.4f; Group.MaxPercent = 1.f;
		}
		{
			FSWGCraftingExperimentGroup& Group = Groups.AddDefaulted_GetRef();
			Group.Title = TEXT("exp_flavor"); Group.CurrentPercent = 0.15f; Group.MaxPercent = 0.8f;
		}
		Crafting->InjectSlotsAndExperiment(Slots, Groups, 40, 22);
		UE_LOG(LogSWGCrafting, Log, TEXT("swg.Craft.Fake slots: injected %d slots, %d experiment groups, state=%d"), Crafting->GetSlots().Num(), Crafting->GetExperimentGroups().Num(), static_cast<int32>(Crafting->GetState()));
	}));
