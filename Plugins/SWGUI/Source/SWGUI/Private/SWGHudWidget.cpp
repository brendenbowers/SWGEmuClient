#include "SWGHudWidget.h"
#include "SWGActionBarWidget.h"
#include "SWGConditionWidget.h"
#include "SWGFloatingTextWidget.h"
#include "SWGHoloInventoryWidget.h"
#include "SWGUISubsystem.h"
#include "SWGUISettings.h"
#include "Objects/Player/SWGPlayer.h"
#include "CommonInputSubsystem.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectIterator.h"

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
		if (TSubclassOf<USWGFloatingTextWidget> FloatingTextClass = USWGUISettings::Get().FloatingTextClass.LoadSynchronous())
		{
			FloatingText = CreateWidget<USWGFloatingTextWidget>(PlayerController, FloatingTextClass);
			FloatingText->AddToPlayerScreen(50);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGHudWidget: no FloatingTextClass set in Project Settings > SWG UI"));
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
		Previous->OnToggleWaypointList.RemoveAll(this);
		Previous->OnToggleDatapad.RemoveAll(this);
		Previous->OnTogglePlanetMap.RemoveAll(this);
	}

	HotkeySource = Cast<ASWGPlayer>(Pawn);
	if (ASWGPlayer* Player = HotkeySource.Get())
	{
		Player->OnActionSlotHotkey.AddUObject(this, &USWGHudWidget::HandleActionSlotHotkey);
		Player->OnActionBankChanged.AddUObject(this, &USWGHudWidget::HandleActionBankChanged);
		Player->OnToggleInventory.AddUObject(this, &USWGHudWidget::ToggleInventory);
		Player->OnToggleWaypointList.AddUObject(this, &USWGHudWidget::ToggleWaypointList);
		Player->OnToggleDatapad.AddUObject(this, &USWGHudWidget::ToggleDatapad);
		Player->OnTogglePlanetMap.AddUObject(this, &USWGHudWidget::TogglePlanetMap);
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
	// Windows belong to the UI subsystem, which stacks them above the layout.
	if (USWGUISubsystem* UI = ULocalPlayer::GetSubsystem<USWGUISubsystem>(GetOwningLocalPlayer()))
	{
		UI->ToggleInventory();
	}
}

void USWGHudWidget::ToggleWaypointList()
{
	if (USWGUISubsystem* UI = ULocalPlayer::GetSubsystem<USWGUISubsystem>(GetOwningLocalPlayer()))
	{
		UI->ToggleWaypointList();
	}
}

void USWGHudWidget::TogglePlanetMap()
{
	if (USWGUISubsystem* UI = ULocalPlayer::GetSubsystem<USWGUISubsystem>(GetOwningLocalPlayer()))
	{
		UI->TogglePlanetMap();
	}
}

void USWGHudWidget::ToggleDatapad()
{
	if (USWGUISubsystem* UI = ULocalPlayer::GetSubsystem<USWGUISubsystem>(GetOwningLocalPlayer()))
	{
		UI->ToggleDatapad();
	}
}

namespace
{
	FAutoConsoleCommand CmdToggleInventory(
		TEXT("swg.Inventory"),
		TEXT("Opens or closes the inventory, as the inventory key does. 'swg.Inventory dock' or 'window' forces that form regardless of the input device."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			USWGHudWidget* Hud = USWGHudWidget::GetActiveHud();
			USWGUISubsystem* UI = Hud ? ULocalPlayer::GetSubsystem<USWGUISubsystem>(Hud->GetOwningLocalPlayer()) : nullptr;
			if (!UI)
			{
				return;
			}
			if (Args.IsEmpty())
			{
				UI->ToggleInventory();
			}
			else
			{
				UI->CloseInventory();
				UI->OpenInventory(Args[0].Equals(TEXT("dock"), ESearchCase::IgnoreCase));
			}
		}));

	FAutoConsoleCommand CmdToggleWaypoints(
		TEXT("swg.Waypoints"),
		TEXT("Opens or closes the waypoint list, as the waypoint list key does."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			USWGHudWidget* Hud = USWGHudWidget::GetActiveHud();
			if (USWGUISubsystem* UI = Hud ? ULocalPlayer::GetSubsystem<USWGUISubsystem>(Hud->GetOwningLocalPlayer()) : nullptr)
			{
				UI->ToggleWaypointList();
			}
		}));

	FAutoConsoleCommand CmdTogglePlanetMap(
		TEXT("swg.Map"),
		TEXT("Opens or closes the planet map, as the planet map key does."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			USWGHudWidget* Hud = USWGHudWidget::GetActiveHud();
			if (USWGUISubsystem* UI = Hud ? ULocalPlayer::GetSubsystem<USWGUISubsystem>(Hud->GetOwningLocalPlayer()) : nullptr)
			{
				UI->TogglePlanetMap();
			}
		}));

	FAutoConsoleCommand CmdPlanetMapMode(
		TEXT("swg.MapMode"),
		TEXT("Chooses the planet map's form: 'swg.MapMode window' or 'swg.MapMode holo'. An open map swaps in place."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			USWGHudWidget* Hud = USWGHudWidget::GetActiveHud();
			USWGUISubsystem* UI = Hud ? ULocalPlayer::GetSubsystem<USWGUISubsystem>(Hud->GetOwningLocalPlayer()) : nullptr;
			if (UI && !Args.IsEmpty())
			{
				UI->SetPlanetMapMode(Args[0].StartsWith(TEXT("holo"), ESearchCase::IgnoreCase) ? ESWGPlanetMapMode::Hologram : ESWGPlanetMapMode::Window);
			}
		}));

	FAutoConsoleCommand CmdInventoryMode(
		TEXT("swg.InvMode"),
		TEXT("Chooses the inventory's form: 'swg.InvMode window' or 'swg.InvMode holo'. An open inventory swaps in place."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			USWGHudWidget* Hud = USWGHudWidget::GetActiveHud();
			USWGUISubsystem* UI = Hud ? ULocalPlayer::GetSubsystem<USWGUISubsystem>(Hud->GetOwningLocalPlayer()) : nullptr;
			if (UI && !Args.IsEmpty())
			{
				UI->SetInventoryMode(Args[0].StartsWith(TEXT("holo"), ESearchCase::IgnoreCase) ? ESWGInventoryMode::Hologram : ESWGInventoryMode::Window);
			}
		}));

	FAutoConsoleCommand CmdHoloMode(
		TEXT("swg.HoloMode"),
		TEXT("Sets every window that has a holographic form at once: 'swg.HoloMode on' or 'swg.HoloMode off'."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			USWGHudWidget* Hud = USWGHudWidget::GetActiveHud();
			USWGUISubsystem* UI = Hud ? ULocalPlayer::GetSubsystem<USWGUISubsystem>(Hud->GetOwningLocalPlayer()) : nullptr;
			if (UI && !Args.IsEmpty())
			{
				const bool bHolo = Args[0].Equals(TEXT("on"), ESearchCase::IgnoreCase) || Args[0].StartsWith(TEXT("holo"), ESearchCase::IgnoreCase) || Args[0] == TEXT("1");
				UI->SetPlanetMapMode(bHolo ? ESWGPlanetMapMode::Hologram : ESWGPlanetMapMode::Window);
				UI->SetInventoryMode(bHolo ? ESWGInventoryMode::Hologram : ESWGInventoryMode::Window);
			}
		}));

	FAutoConsoleCommand CmdHoloInventorySelect(
		TEXT("swg.HoloInventory.Select"),
		TEXT("Pins the next ('swg.HoloInventory.Select 1') or previous (-1) worn item's details on an open holo inventory, as the D-pad does; 'swg.HoloInventory.Select bag 1' steps through the bag instead."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bBag = !Args.IsEmpty() && Args[0].Equals(TEXT("bag"), ESearchCase::IgnoreCase);
			const int32 DirectionArg = bBag ? 1 : 0;
			const int32 Direction = Args.IsValidIndex(DirectionArg) ? FCString::Atoi(*Args[DirectionArg]) : 1;
			for (TObjectIterator<USWGHoloInventoryWidget> It; It; ++It)
			{
				// Only one that is on screen; the class default object never is.
				if (It->GetCachedWidget().IsValid())
				{
					if (bBag)
					{
						It->SelectNextInBag(Direction);
					}
					else
					{
						It->SelectNext(Direction);
					}
				}
			}
		}));

	FAutoConsoleCommand CmdHoloInventoryPane(
		TEXT("swg.HoloInventory.Pane"),
		TEXT("Turns an open holo inventory's pane to the bag ('swg.HoloInventory.Pane inventory') or the character sheet ('swg.HoloInventory.Pane character')."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bCharacter = !Args.IsEmpty() && Args[0].StartsWith(TEXT("char"), ESearchCase::IgnoreCase);
			for (TObjectIterator<USWGHoloInventoryWidget> It; It; ++It)
			{
				if (It->GetCachedWidget().IsValid())
				{
					It->ShowPane(bCharacter ? ESWGHoloInventoryPane::Character : ESWGHoloInventoryPane::Inventory);
				}
			}
		}));

	FAutoConsoleCommand CmdToggleDatapad(
		TEXT("swg.Datapad"),
		TEXT("Opens or closes the datapad, as the datapad key does."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			USWGHudWidget* Hud = USWGHudWidget::GetActiveHud();
			if (USWGUISubsystem* UI = Hud ? ULocalPlayer::GetSubsystem<USWGUISubsystem>(Hud->GetOwningLocalPlayer()) : nullptr)
			{
				UI->ToggleDatapad();
			}
		}));
}
