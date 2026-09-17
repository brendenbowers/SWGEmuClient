#include "SWGHudWidget.h"
#include "SWGActionBarWidget.h"
#include "SWGConditionWidget.h"
#include "SWGFloatingTextWidget.h"
#include "SWGGameLayout.h"
#include "SWGInventoryWidget.h"
#include "SWGUISettings.h"
#include "Objects/Player/SWGPlayer.h"
#include "CommonInputSubsystem.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/LocalPlayer.h"
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

	// The bar's height depends on its layout (one row of slots, or three-row
	// diamonds), so size it to content: anchored to the bottom edge, it then
	// grows upward instead of overflowing off-screen.
	if (UCanvasPanelSlot* BarSlot = ActionBar ? Cast<UCanvasPanelSlot>(ActionBar->Slot) : nullptr)
	{
		BarSlot->SetAutoSize(true);
	}

	// Below the layout (100) so damage numbers never cover a window.
	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		TSubclassOf<USWGFloatingTextWidget> FloatingTextClass = USWGUISettings::Get().FloatingTextClass.LoadSynchronous();
		FloatingText = CreateWidget<USWGFloatingTextWidget>(PlayerController, FloatingTextClass ? *FloatingTextClass : USWGFloatingTextWidget::StaticClass());
		if (FloatingText)
		{
			FloatingText->AddToPlayerScreen(50);
		}
	}

	if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetOwningLocalPlayer()))
	{
		InputMethodChangedHandle = CommonInput->OnInputMethodChangedNative.AddUObject(this, &USWGHudWidget::HandleInputMethodChanged);
		HandleInputMethodChanged(CommonInput->GetCurrentInputType());
	}
}

void USWGHudWidget::NativeDestruct()
{
	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		PlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &USWGHudWidget::HandlePossessedPawnChanged);
	}
	BindHotkeys(nullptr);

	if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetOwningLocalPlayer()))
	{
		CommonInput->OnInputMethodChangedNative.Remove(InputMethodChangedHandle);
	}
	InputMethodChangedHandle.Reset();

	if (FloatingText)
	{
		FloatingText->RemoveFromParent();
		FloatingText = nullptr;
	}

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
		Previous->OnActionBankChanged.RemoveAll(this);
		Previous->OnToggleInventory.RemoveAll(this);
	}

	HotkeySource = Cast<ASWGPlayer>(Pawn);
	if (ASWGPlayer* Player = HotkeySource.Get())
	{
		Player->OnActionSlotHotkey.AddUObject(this, &USWGHudWidget::HandleActionSlotHotkey);
		Player->OnActionBankChanged.AddUObject(this, &USWGHudWidget::HandleActionBankChanged);
		Player->OnToggleInventory.AddUObject(this, &USWGHudWidget::ToggleInventory);
		HandleActionBankChanged(Player->GetActiveActionBank());
	}
}

void USWGHudWidget::HandleActionSlotHotkey(int32 SlotIndex)
{
	TriggerActionSlot(SlotIndex);
}

void USWGHudWidget::HandleActionBankChanged(int32 BankIndex)
{
	if (ActionBar)
	{
		ActionBar->SetActiveBank(BankIndex);
	}
}

void USWGHudWidget::HandleInputMethodChanged(ECommonInputType InputType)
{
	if (ActionBar)
	{
		ActionBar->SetGamepadLayout(InputType == ECommonInputType::Gamepad);
	}
}

bool USWGHudWidget::TriggerActionSlot(int32 SlotIndex)
{
	return ActionBar ? ActionBar->TriggerSlot(SlotIndex) : false;
}

void USWGHudWidget::ToggleInventory()
{
	// The window closes itself on Escape too, so "open" means still activated.
	if (USWGInventoryWidget* Open = InventoryWindow.Get())
	{
		const bool bWasOpen = Open->IsActivated();
		Open->DeactivateWidget();
		InventoryWindow.Reset();
		if (bWasOpen)
		{
			return;
		}
	}

	USWGGameLayout* Layout = USWGGameLayout::GetLayout(this);
	if (!Layout)
	{
		return;
	}

	TSubclassOf<USWGInventoryWidget> InventoryClass = USWGUISettings::Get().InventoryClass.LoadSynchronous();
	InventoryWindow = Cast<USWGInventoryWidget>(Layout->PushWidgetToLayerStack(
		USWGGameLayout::TAG_Layer_Modal, InventoryClass ? *InventoryClass : USWGInventoryWidget::StaticClass()));
}

namespace
{
	FAutoConsoleCommand CmdToggleInventory(
		TEXT("swg.Inventory"),
		TEXT("Opens or closes the inventory window, as the inventory key does."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (USWGHudWidget* Hud = USWGHudWidget::GetActiveHud())
			{
				Hud->ToggleInventory();
			}
		}));
}
