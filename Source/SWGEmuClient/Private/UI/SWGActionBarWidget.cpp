#include "UI/SWGActionBarWidget.h"
#include "Components/SWGCombatStateComponent.h"
#include "Components/SWGSkillComponent.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Engine/GameInstance.h"

namespace
{
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
}

void USWGActionBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BuildSlotWidgets();

	if (bFillEmptySlotsFromAbilities)
	{
		FillEmptySlotsFromAbilities();
	}

	RefreshSlotVisuals();
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

	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		USWGActionSlotWidget* SlotWidget = CreateWidget<USWGActionSlotWidget>(this, SlotWidgetClass);
		if (!SlotWidget)
		{
			continue;
		}

		SlotWidget->InitialiseSlot(this, SlotIndex, GetSlotKeyLabel(SlotIndex));
		SlotBox->AddChild(SlotWidget);
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
			continue;
		}

		const FSWGActionSlot& SlotData = Slots[SlotIndex];
		SlotWidget->SetCommandLabel(SlotData.Label.IsEmpty() ? FText::FromString(SlotData.CommandName) : SlotData.Label);
	}
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

	USWGCommandSubsystem* Commands = GetCommands(this);
	if (!Commands || Commands->SendCommand(Slots[SlotIndex].CommandName, ResolveTargetId()) == 0)
	{
		return false;
	}

	OnSlotTriggered(SlotIndex);
	return true;
}

bool USWGActionBarWidget::SendCommand(const FString& CommandName, const FString& Arguments)
{
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

	return Skills ? Skills->AbilityList.Items : TArray<FString>();
}
