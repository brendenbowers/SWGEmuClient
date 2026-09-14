#include "SWGActionBarWidget.h"
#include "Components/SWGCombatStateComponent.h"
#include "Components/SWGSkillComponent.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGCombatSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"

namespace
{
	USWGTreSubsystem* GetTre(const UWidget* Widget)
	{
		UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	}

	USWGObjectGraphSubsystem* GetObjectGraph(const UWidget* Widget)
	{
		UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	}

	USWGCommandSubsystem* GetCommands(const UWidget* Widget)
	{
		UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGCommandSubsystem>() : nullptr;
	}

	USWGCombatSubsystem* GetCombat(const UWidget* Widget)
	{
		UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGCombatSubsystem>() : nullptr;
	}
}

void USWGActionBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BuildSlotWidgets();

	// Before the ability fill, so attack keeps slot 0 rather than being
	// pushed along by whatever the player happens to know.
	if (bSeedDefaultAttackSlot)
	{
		SeedDefaultAttackSlot();
	}

	if (bFillEmptySlotsFromAbilities)
	{
		FillEmptySlotsFromAbilities();
	}

	RefreshSlotVisuals();
}

void USWGActionBarWidget::SeedDefaultAttackSlot()
{
	const USWGCombatSubsystem* Combat = GetCombat(this);
	const FString AttackCommand = Combat ? Combat->AttackCommandName : TEXT("attack");

	if (Slots.Num() < SlotCount)
	{
		Slots.SetNum(SlotCount);
	}

	if (Slots.IsEmpty() || !Slots[0].IsEmpty())
	{
		return;
	}

	Slots[0].CommandName = AttackCommand;
}

int32 USWGActionBarWidget::FillEmptySlotsFromAbilities()
{
	const TArray<FString> Abilities = GetAvailableAbilities();
	if (Abilities.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("USWGActionBarWidget: no abilities to fill from — the player object's base9 list is empty"));
		return 0;
	}

	if (Slots.Num() < SlotCount)
	{
		Slots.SetNum(SlotCount);
	}

	int32 FilledCount = 0;
	int32 NextAbility = 0;

	for (FSWGActionSlot& SlotData : Slots)
	{
		if (!SlotData.IsEmpty())
		{
			continue;
		}

		// Skip anything already sitting in another slot.
		while (Abilities.IsValidIndex(NextAbility) &&
			Slots.ContainsByPredicate([&Abilities, NextAbility](const FSWGActionSlot& Existing)
			{
				return Existing.CommandName == Abilities[NextAbility];
			}))
		{
			++NextAbility;
		}

		if (!Abilities.IsValidIndex(NextAbility))
		{
			break;
		}

		SlotData.CommandName = Abilities[NextAbility++];
		++FilledCount;
	}

	UE_LOG(LogTemp, Log, TEXT("USWGActionBarWidget: filled %d slot(s) from %d available ability/abilities"), FilledCount, Abilities.Num());

	RefreshSlotVisuals();
	return FilledCount;
}

FText USWGActionBarWidget::GetSlotKeyLabel(int32 SlotIndex)
{
	// 1-9, then 0, then the two keys past it, matching SWG's numbering.
	static const TCHAR* Keys[] = { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"),
								   TEXT("7"), TEXT("8"), TEXT("9"), TEXT("0"), TEXT("-"), TEXT("=") };

	return Keys && SlotIndex >= 0 && SlotIndex < UE_ARRAY_COUNT(Keys)
		? FText::FromString(Keys[SlotIndex])
		: FText::GetEmpty();
}

void USWGActionBarWidget::BuildSlotWidgets()
{
	if (!SlotBox || !SlotWidgetClass)
	{
		// Silent here means an empty bar with nothing to explain it — the usual
		// cause is the widget instance overriding SlotWidgetClass with None.
		UE_LOG(LogTemp, Warning, TEXT("USWGActionBarWidget: no slots built — SlotBox %s, SlotWidgetClass %s"),
			SlotBox ? TEXT("bound") : TEXT("MISSING"),
			SlotWidgetClass ? TEXT("set") : TEXT("MISSING"));
		return;
	}

	SlotBox->ClearChildren();
	SlotWidgets.Reset();

	const FSlateBrush* FrameBrush = ResolveStyleBrush(SlotFrameStyle);
	const int32 SlotsPerGroup = FMath::Max(1, GroupSize);
	UHorizontalBox* Group = nullptr;

	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		if (SlotIndex % SlotsPerGroup == 0)
		{
			Group = WidgetTree->ConstructWidget<UHorizontalBox>();
			if (UHorizontalBoxSlot* GroupSlot = Cast<UHorizontalBoxSlot>(SlotBox->AddChild(Group)); GroupSlot && SlotIndex > 0)
			{
				GroupSlot->SetPadding(FMargin(GroupSpacing, 0.f, 0.f, 0.f));
			}
		}

		USWGActionSlotWidget* SlotWidget = CreateWidget<USWGActionSlotWidget>(this, SlotWidgetClass);
		if (!SlotWidget)
		{
			continue;
		}

		SlotWidget->InitialiseSlot(this, SlotIndex, GetSlotKeyLabel(SlotIndex));
		SlotWidget->SetFrame(FrameBrush);
		Group->AddChildToHorizontalBox(SlotWidget);
		SlotWidgets.Add(SlotWidget);
	}
}

void USWGActionBarWidget::RefreshSlotVisuals()
{
	for (int32 SlotIndex = 0; SlotIndex < SlotWidgets.Num(); ++SlotIndex)
	{
		USWGActionSlotWidget* SlotWidget = SlotWidgets[SlotIndex];
		if (!SlotWidget)
		{
			continue;
		}

		if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
		{
			SlotWidget->SetCommandLabel(FText::GetEmpty());
			SlotWidget->SetCommandIcon(nullptr);
			continue;
		}

		const FSWGActionSlot& SlotData = Slots[SlotIndex];
		const FText Label = SlotData.Label.IsEmpty() ? ResolveCommandName(SlotData.CommandName) : SlotData.Label;
		const FSlateBrush* Icon = ResolveCommandIcon(SlotData.CommandName);
		SlotWidget->SetCommandLabel(Label);
		SlotWidget->SetCommandIcon(Icon);

		UE_LOG(LogTemp, Verbose, TEXT("USWGActionBarWidget: slot %d '%s' -> '%s', icon %s"),
			SlotIndex, *SlotData.CommandName, *Label.ToString(), Icon ? TEXT("yes") : TEXT("none"));
	}
}

FText USWGActionBarWidget::ResolveCommandName(const FString& CommandName) const
{
	USWGTreSubsystem* Tre = GetTre(this);
	if (!Tre)
	{
		return FText::FromString(CommandName);
	}

	FString Name = CommandName.ToLower();
	FString Display = Tre->LookupString(TEXT("cmd_n"), Name);
	if (Display.IsEmpty() && Name.RemoveFromEnd(TEXT("server")))
	{
		// Server-side commands the client wraps ("sitserver") are named after the client command.
		Display = Tre->LookupString(TEXT("cmd_n"), Name);
	}
	return FText::FromString(Display.IsEmpty() ? CommandName : Display);
}

namespace
{
	const FSlateBrush* MakeStyleBrush(USWGTreSubsystem* Tre, const FSWGUIImageStyle* Style, const FString& CacheKey, TMap<FString, FSlateBrush>& Cache)
	{
		UTexture2D* Sheet = (Tre && Style) ? Tre->GetOrLoadTexture(FString::Printf(TEXT("texture/%s.dds"), *Style->Source)) : nullptr;
		if (!Sheet || Sheet->GetSizeX() <= 0 || Sheet->GetSizeY() <= 0)
		{
			return nullptr;
		}

		// The style's rect is in sheet pixels; the brush wants it as a UV box.
		const FVector2D SheetSize(Sheet->GetSizeX(), Sheet->GetSizeY());
		const FBox2D UVRegion(FVector2D(Style->SourceRect.Min) / SheetSize, FVector2D(Style->SourceRect.Max) / SheetSize);

		FSlateBrush Brush;
		Brush.SetResourceObject(Sheet);
		Brush.ImageSize = FVector2D(Style->SourceRect.Size());
		Brush.SetUVRegion(UVRegion);
		return &Cache.Add(CacheKey, MoveTemp(Brush));
	}
}

const FSlateBrush* USWGActionBarWidget::ResolveCommandIcon(const FString& CommandName)
{
	if (CommandName.IsEmpty())
	{
		return nullptr;
	}

	// Prefixed so a command can't collide with a style path in the shared cache.
	const FString Key = TEXT("command:") + CommandName.ToLower();
	if (const FSlateBrush* Cached = StyleBrushes.Find(Key))
	{
		return Cached;
	}

	USWGTreSubsystem* Tre = GetTre(this);
	return MakeStyleBrush(Tre, Tre ? Tre->FindCommandIcon(CommandName) : nullptr, Key, StyleBrushes);
}

const FSlateBrush* USWGActionBarWidget::ResolveStyleBrush(const FString& DottedPath)
{
	if (DottedPath.IsEmpty())
	{
		return nullptr;
	}

	const FString Key = DottedPath.ToLower();
	if (const FSlateBrush* Cached = StyleBrushes.Find(Key))
	{
		return Cached;
	}

	USWGTreSubsystem* Tre = GetTre(this);
	const FSWGUIStyleSheet* Sheet = Tre ? Tre->GetUIStyleSheet() : nullptr;
	return MakeStyleBrush(Tre, Sheet ? Sheet->FindImageStyle(Key) : nullptr, Key, StyleBrushes);
}

int64 USWGActionBarWidget::ResolveTargetId() const
{
	USWGObjectGraphSubsystem* ObjectGraph = GetObjectGraph(this);
	if (!ObjectGraph)
	{
		return 0;
	}

	const USWGCombatStateComponent* CombatState =
		ObjectGraph->FindComponent<USWGCombatStateComponent>(ObjectGraph->GetLocalPlayerObjectId());

	return CombatState ? CombatState->TargetId : 0;
}

bool USWGActionBarWidget::TriggerSlot(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
	{
		return false;
	}

	if (!SendCommand(Slots[SlotIndex].CommandName, FString()))
	{
		return false;
	}

	OnSlotTriggered(SlotIndex);
	return true;
}

bool USWGActionBarWidget::SendCommand(const FString& CommandName, const FString& Arguments)
{
	// The basic attack is the one command with client-side repeat semantics —
	// the server runs a queued command once and drains its queue, so sustained
	// combat is the client re-sending. Route it through the combat subsystem,
	// which owns that loop, and let the press toggle it the way SWG's toolbar
	// does. Everything else is a plain one-shot.
	USWGCombatSubsystem* Combat = GetCombat(this);
	if (Combat && Arguments.IsEmpty() && CommandName.Equals(Combat->AttackCommandName, ESearchCase::IgnoreCase))
	{
		// True means "the press did something", not "an attack started" —
		// stopping one is just as much a handled press, and the slot should
		// still flash for it.
		const bool bWasAttacking = Combat->IsAttacking();
		return Combat->ToggleAttack(ResolveTargetId()) || bWasAttacking;
	}

	USWGCommandSubsystem* Commands = GetCommands(this);
	return Commands && Commands->SendCommand(CommandName, ResolveTargetId(), Arguments) != 0;
}

void USWGActionBarWidget::SetSlotCommand(int32 SlotIndex, const FString& CommandName, FText Label)
{
	if (SlotIndex < 0)
	{
		return;
	}

	if (!Slots.IsValidIndex(SlotIndex))
	{
		Slots.SetNum(SlotIndex + 1);
	}

	Slots[SlotIndex].CommandName = CommandName;
	Slots[SlotIndex].Label = Label;

	RefreshSlotVisuals();
}

TArray<FString> USWGActionBarWidget::GetAvailableAbilities() const
{
	USWGObjectGraphSubsystem* ObjectGraph = GetObjectGraph(this);
	if (!ObjectGraph)
	{
		return {};
	}

	const USWGSkillComponent* Skills =
		ObjectGraph->FindComponent<USWGSkillComponent>(ObjectGraph->GetLocalPlayerObjectId());

	if (!Skills)
	{
		return {};
	}

	// The ability list also carries skill markers like "private_brawler_novice",
	// which aren't commands — a slot holding one is a button that does nothing.
	const USWGCommandSubsystem* Commands = GetCommands(this);
	if (!Commands)
	{
		return Skills->AbilityList.Items;
	}

	return Skills->AbilityList.Items.FilterByPredicate(
		[Commands](const FString& Ability) { return Commands->IsKnownCommand(Ability); });
}
