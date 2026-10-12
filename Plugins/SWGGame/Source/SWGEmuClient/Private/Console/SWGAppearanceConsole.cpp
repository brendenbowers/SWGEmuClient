#include "Components/SWGEquipmentComponent.h"
#include "Objects/Creature/SWGCreature.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTargetSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGCustomizationTables.h"
#include "TRE/SWGDataTableReader.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace
{
	// From the editor's console the world is the editor's; use the running game's instead.
	UGameInstance* FindRunningGameInstance(UWorld* World)
	{
		if (World && World->GetGameInstance())
		{
			return World->GetGameInstance();
		}
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.OwningGameInstance)
				{
					return Context.OwningGameInstance;
				}
			}
		}
		return nullptr;
	}

	/** "self", an object id, or nothing (the current target, else the player). */
	ASWGCreature* FindCreature(UGameInstance& GameInstance, const TArray<FString>& Args)
	{
		const USWGObjectGraphSubsystem* ObjectGraph = GameInstance.GetSubsystem<USWGObjectGraphSubsystem>();
		if (!ObjectGraph)
		{
			return nullptr;
		}
		int64 ObjectId = 0;
		const bool bSelf = !Args.IsEmpty() && Args[0].Equals(TEXT("self"), ESearchCase::IgnoreCase);
		if (!Args.IsEmpty() && !bSelf && Args[0].IsNumeric())
		{
			ObjectId = FCString::Atoi64(*Args[0]);
		}
		else if (!bSelf)
		{
			if (const USWGTargetSubsystem* Targeting = GameInstance.GetSubsystem<USWGTargetSubsystem>())
			{
				ObjectId = Targeting->GetTargetId();
			}
		}
		if (ObjectId == 0)
		{
			ObjectId = ObjectGraph->GetLocalPlayerObjectId();
		}
		return Cast<ASWGCreature>(ObjectGraph->FindActor(ObjectId));
	}

	void LogLines(const TCHAR* Prefix, const TArray<FString>& Lines)
	{
		for (const FString& Line : Lines)
		{
			UE_LOG(LogTemp, Display, TEXT("%s %s"), Prefix, *Line);
		}
	}

	bool ReadTable(USWGTreSubsystem& Tre, const TCHAR* Path, FSWGDataTableData& OutTable)
	{
		return FSWGDataTableReader::ReadDataTable(Tre.CreateIffReader(Path), OutTable);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAppearanceWearablesCommand(
	TEXT("swg.Appearance.Wearables"),
	TEXT("swg.Appearance.Wearables [self|<objectId>] [dump] - worn items of the target (else the player): slots, attached meshes and the slot-rule morphs they get."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = FindRunningGameInstance(World);
		ASWGCreature* Creature = GameInstance ? FindCreature(*GameInstance, Args) : nullptr;
		if (!Creature || !Creature->EquipmentComponent)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Appearance.Wearables: needs a running game with a target or player creature"));
			return;
		}
		LogLines(TEXT("swg.Appearance.Wearables:"), Creature->EquipmentComponent->DescribeWearables());
	}));

static FAutoConsoleCommandWithWorldAndArgs GAppearanceCustomizationCommand(
	TEXT("swg.Appearance.Customization"),
	TEXT("swg.Appearance.Customization [self|<objectId>] [dump] - how the target's (else the player's) customization resolved: variables, texture renderers and what they read. Re-applies the look first."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = FindRunningGameInstance(World);
		ASWGCreature* Creature = GameInstance ? FindCreature(*GameInstance, Args) : nullptr;
		USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
		if (!Creature || !MeshGenerator)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Appearance.Customization: needs a running game with a target or player creature"));
			return;
		}
		MeshGenerator->RefreshCharacterAppearance(*Creature);
		LogLines(TEXT("swg.Appearance.Customization:"), MeshGenerator->DescribeCharacterCustomization(*Creature));
	}));

static FAutoConsoleCommandWithWorldAndArgs GAppearanceTablesCommand(
	TEXT("swg.Appearance.Tables"),
	TEXT("swg.Appearance.Tables - summary of the appearance tables: slot rules, wearable appearance map, allow_bald, customization groups/data, palette columns, hair assets."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = FindRunningGameInstance(World);
		USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
		USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
		if (!Tre || !MeshGenerator)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Appearance.Tables: needs a running game"));
			return;
		}

		if (const FSWGSlotRuleManager* Rules = MeshGenerator->GetSlotRuleManager())
		{
			UE_LOG(LogTemp, Display, TEXT("slot rules: %d binding(s), %d node(s)"), Rules->Bindings.Num(), Rules->Nodes.Num());
			for (const FSWGSlotRuleBinding& Binding : Rules->Bindings)
			{
				UE_LOG(LogTemp, Display, TEXT("  slot %s -> %s"), *Binding.SlotName, *Binding.VariableName);
			}
		}
		if (const FSWGWearableAppearanceMap* Map = MeshGenerator->GetWearableAppearanceMap())
		{
			UE_LOG(LogTemp, Display, TEXT("wearable appearance map: %d entr(ies)"), Map->Num());
		}

		FSWGDataTableData Table;
		FSWGAllowBaldTable Bald;
		if (ReadTable(*Tre, TEXT("datatables/customization/allow_bald.iff"), Table) && FSWGAllowBaldTable::Read(Table, Bald))
		{
			FString Disallowed;
			for (const TPair<FString, bool>& Pair : Bald.GetAll())
			{
				Disallowed += Pair.Value ? FString() : Pair.Key + TEXT(" ");
			}
			UE_LOG(LogTemp, Display, TEXT("allow_bald: %d species, bald not allowed for: %s"), Bald.GetAll().Num(), *Disallowed);
		}
		FSWGDataTableData SharedTable, InstanceTable;
		FSWGCustomizationGroupTable Groups;
		if (ReadTable(*Tre, TEXT("datatables/customization/customization_group_shared.iff"), SharedTable)
			&& ReadTable(*Tre, TEXT("datatables/customization/customization_group_instance.iff"), InstanceTable)
			&& FSWGCustomizationGroupTable::Read(SharedTable, InstanceTable, Groups))
		{
			UE_LOG(LogTemp, Display, TEXT("customization groups: %d shared, %d instance row(s)"), Groups.Shared.Num(), Groups.Instances.Num());
		}
		FSWGCustomizationDataTable Data;
		if (ReadTable(*Tre, TEXT("datatables/customization/customization_data.iff"), Table) && FSWGCustomizationDataTable::Read(Table, Data))
		{
			UE_LOG(LogTemp, Display, TEXT("customization_data: %d row(s), human_male has %d"), Data.Rows.Num(), Data.GetRowsFor(TEXT("human_male")).Num());
		}
		FSWGPaletteColumnsTable Columns;
		if (ReadTable(*Tre, TEXT("datatables/customization/palette_columns.iff"), Table) && FSWGPaletteColumnsTable::Read(Table, Columns))
		{
			UE_LOG(LogTemp, Display, TEXT("palette_columns: %d palette(s)"), Columns.Rows.Num());
		}
		FSWGHairAssetTable Hair;
		if (ReadTable(*Tre, TEXT("datatables/customization/hair_assets_skill_mods.iff"), Table) && FSWGHairAssetTable::Read(Table, Hair))
		{
			UE_LOG(LogTemp, Display, TEXT("hair assets: %d row(s)"), Hair.Rows.Num());
		}
	}));

#endif // !UE_BUILD_SHIPPING
