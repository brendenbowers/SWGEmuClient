#include "UI/SWGHudWidget.h"
#include "UI/SWGActionBarWidget.h"
#include "UI/SWGConditionWidget.h"
#include "Objects/Player/SWGPlayer.h"
#include "GameFramework/PlayerController.h"

TWeakObjectPtr<USWGHudWidget> USWGHudWidget::ActiveHud;

USWGHudWidget* USWGHudWidget::GetActiveHud()
{
	return ActiveHud.Get();
}

void USWGHudWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ActiveHud = this;

	// The pawn may be possessed before or after the HUD comes up (and swapped on
	// zone travel), so follow possession rather than binding once.
	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		PlayerController->OnPossessedPawnChanged.AddUniqueDynamic(this, &USWGHudWidget::HandlePossessedPawnChanged);
		BindHotkeys(PlayerController->GetPawn());
	}
}

void USWGHudWidget::NativeDestruct()
{
	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		PlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &USWGHudWidget::HandlePossessedPawnChanged);
	}
	BindHotkeys(nullptr);

	if (ActiveHud.Get() == this)
	{
		ActiveHud.Reset();
	}

	Super::NativeDestruct();
}

void USWGHudWidget::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	BindHotkeys(NewPawn);
}

void USWGHudWidget::BindHotkeys(APawn* Pawn)
{
	if (ASWGPlayer* Previous = HotkeySource.Get())
	{
		Previous->OnActionSlotHotkey.RemoveAll(this);
	}

	HotkeySource = Cast<ASWGPlayer>(Pawn);
	if (ASWGPlayer* Player = HotkeySource.Get())
	{
		Player->OnActionSlotHotkey.AddUObject(this, &USWGHudWidget::HandleActionSlotHotkey);
	}
}

void USWGHudWidget::HandleActionSlotHotkey(int32 SlotIndex)
{
	TriggerActionSlot(SlotIndex);
}

bool USWGHudWidget::TriggerActionSlot(int32 SlotIndex)
{
	return ActionBar ? ActionBar->TriggerSlot(SlotIndex) : false;
}
