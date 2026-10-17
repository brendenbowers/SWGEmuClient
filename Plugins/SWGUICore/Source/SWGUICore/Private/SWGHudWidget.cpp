#include "SWGHudWidget.h"
#include "SWGActionBarWidget.h"
#include "SWGConditionWidget.h"
#include "SWGFloatingTextWidget.h"
#include "SWGUISubsystem.h"
#include "SWGCoreUISettings.h"
#include "Objects/Player/SWGPlayer.h"
#include "CommonInputSubsystem.h"
#include "Input/UIActionBindingHandle.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectIterator.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Brushes/SlateColorBrush.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Objects/World/SWGShip.h"
#include "Common/SWGWorldScale.h"

TWeakObjectPtr<USWGHudWidget> USWGHudWidget::ActiveHud;

USWGHudWidget* USWGHudWidget::GetActiveHud()
{
	return ActiveHud.Get();
}

TOptional<FUIInputConfig> USWGHudWidget::GetDesiredInputConfig() const
{
	// CommonUI reapplies this when menus close or the input device changes.
	return FUIInputConfig(ECommonInputMode::All, EMouseCaptureMode::CaptureDuringRightMouseDown, EMouseLockMode::LockOnCapture, true);
}

void USWGHudWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ActiveHud = this;
	if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(GetRootWidget()); ensureMsgf(Canvas, TEXT("SWG HUD requires its canvas root")))
	{
		if (!ShipFlightPanel)
		{
			ShipFlightPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ShipFlightPanel"));
			ShipFlightPanel->SetBrushColor(FLinearColor(.015f, .035f, .05f, .92f));
			ShipFlightPanel->SetPadding(FMargin(16, 12));
			UCanvasPanelSlot* FlightSlot = Canvas->AddChildToCanvas(ShipFlightPanel);
			FlightSlot->SetAnchors(FAnchors(.5f, 1.f));
			FlightSlot->SetAlignment(FVector2D(.5f, 1.f));
			FlightSlot->SetPosition(FVector2D(0, -24));
			FlightSlot->SetAutoSize(true);
			USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
			Size->SetWidthOverride(480);
			ShipFlightPanel->SetContent(Size);
			UVerticalBox* Readouts = WidgetTree->ConstructWidget<UVerticalBox>();
			Size->SetContent(Readouts);
			ShipFlightText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShipFlightText"));
			ShipSpeedGauge = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ShipSpeedGauge"));
			ShipThrottleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShipThrottleText"));
			ShipThrottleGauge = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ShipThrottleGauge"));
			ShipControlsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShipControlsText"));
			for (UTextBlock* Text : {ShipFlightText.Get(), ShipThrottleText.Get(), ShipControlsText.Get()})
			{
				FSlateFontInfo Font = Text->GetFont(); Font.Size = Text == ShipControlsText ? 12 : 16;
				Text->SetFont(Font);
				Text->SetColorAndOpacity(FLinearColor(.8f, .95f, 1.f));
				Text->SetAutoWrapText(true);
			}
			FProgressBarStyle GaugeStyle = ShipSpeedGauge->GetWidgetStyle();
			GaugeStyle.BackgroundImage = FSlateColorBrush(FLinearColor(.04f, .08f, .1f));
			GaugeStyle.BackgroundImage.SetImageSize(FVector2D(1, 8));
			GaugeStyle.FillImage = FSlateColorBrush(FLinearColor::White);
			ShipSpeedGauge->SetWidgetStyle(GaugeStyle);
			ShipThrottleGauge->SetWidgetStyle(GaugeStyle);
			ShipSpeedGauge->SetFillColorAndOpacity(FLinearColor(.1f, .7f, 1.f));
			ShipThrottleGauge->SetFillColorAndOpacity(FLinearColor(1.f, .65f, .15f));
			UWidget* ReadoutWidgets[] = {ShipFlightText, ShipSpeedGauge, ShipThrottleText, ShipThrottleGauge, ShipControlsText};
			for (UWidget* Widget : ReadoutWidgets)
			{
				Readouts->AddChildToVerticalBox(Widget)->SetPadding(FMargin(0, 3));
			}
		}
		ShipFlightPanel->SetVisibility(ESlateVisibility::Collapsed);
		// CommonActivatableWidget disables native tick; keep the readout current without changing its Blueprint tick policy.
		if (UGameInstance* Game = GetGameInstance()) Game->GetTimerManager().SetTimer(FlightReadoutTimer, this, &USWGHudWidget::UpdateFlightReadout, .1f, true);
	}

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
		if (TSubclassOf<USWGFloatingTextWidget> FloatingTextClass = USWGCoreUISettings::Get().FloatingTextClass.LoadSynchronous())
		{
			FloatingText = CreateWidget<USWGFloatingTextWidget>(PlayerController, FloatingTextClass);
			FloatingText->AddToPlayerScreen(50);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGHudWidget: no FloatingTextClass set in Project Settings > SWG UI (Core)"));
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
	if (UGameInstance* Game = GetGameInstance()) Game->GetTimerManager().ClearTimer(FlightReadoutTimer);
	if (USWGUISubsystem* Router = ULocalPlayer::GetSubsystem<USWGUISubsystem>(GetOwningLocalPlayer()))
	{
		if (Router->IsFeatureOpen(ESWGUIFeature::SpaceHud)) Router->CloseFeature(ESWGUIFeature::SpaceHud);
	}
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

void USWGHudWidget::UpdateFlightReadout()
{
	const USWGObjectGraphSubsystem* Graph = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	const bool bPiloting = Graph && Graph->IsLocalPlayerPiloting();
	if (ConditionPanel) ConditionPanel->SetVisibility(bPiloting ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (ActionBar) ActionBar->SetVisibility(bPiloting ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	if (!ShipFlightPanel) return;
	ASWGPlayer* Pilot = Graph ? Cast<ASWGPlayer>(Graph->FindActor(Graph->GetLocalPlayerObjectId())) : nullptr;
	// HUD widgets can survive travel while their controller and pawn are replaced.
	if (HotkeySource.Get() != Pilot) BindHotkeys(Pilot);
	const ASWGShip* Ship = Graph ? Graph->GetLocalPlayerShip() : nullptr;
	const bool bReady = bPiloting && Pilot && Ship && Graph->IsLocalSpaceSceneReady();
	// The holographic piloting HUD replaces this panel once the ship is ready; with no holo presenter the panel stays.
	USWGUISubsystem* Router = ULocalPlayer::GetSubsystem<USWGUISubsystem>(GetOwningLocalPlayer());
	const bool bHoloHud = bReady && Router && Router->CanOpenFeature(ESWGUIFeature::SpaceHud);
	if (Router)
	{
		if (bHoloHud && !Router->IsFeatureOpen(ESWGUIFeature::SpaceHud)) Router->OpenFeature(ESWGUIFeature::SpaceHud, FSWGUIFeatureRequest());
		else if (!bHoloHud && Router->IsFeatureOpen(ESWGUIFeature::SpaceHud)) Router->CloseFeature(ESWGUIFeature::SpaceHud);
	}
	ShipFlightPanel->SetVisibility(bPiloting && !bHoloHud ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!bPiloting) return;
	UWidget* FlightWidgets[] = {ShipSpeedGauge, ShipThrottleText, ShipThrottleGauge, ShipControlsText};
	for (UWidget* Widget : FlightWidgets)
	{
		Widget->SetVisibility(bReady ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (!bReady)
	{
		ShipFlightText->SetText(NSLOCTEXT("SWG", "FlightPreparing", "Preparing ship - waiting for flight readiness"));
		return;
	}
	const float Speed = Ship->SpaceVelocity.Size() / SWGWorldScale;
	const float MaxSpeed = Ship->ShipState.Flight.MaxSpeed;
	const float Throttle = FMath::Clamp(Pilot->GetShipThrottle(), 0.f, 1.f);
	FNumberFormattingOptions SpeedFormat; SpeedFormat.SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
	ShipFlightText->SetText(FText::Format(NSLOCTEXT("SWG", "FlightSpeedReadout", "{0}  |  Speed {1} / {2} m/s"),
		Pilot->IsExteriorShipView() ? NSLOCTEXT("SWG", "ExteriorView", "Exterior") : NSLOCTEXT("SWG", "CockpitView", "Cockpit"),
		FText::AsNumber(Speed, &SpeedFormat), FText::AsNumber(MaxSpeed, &SpeedFormat)));
	ShipSpeedGauge->SetPercent(MaxSpeed > 0 ? FMath::Clamp(Speed / MaxSpeed, 0.f, 1.f) : 0.f);
	ShipThrottleText->SetText(FText::Format(NSLOCTEXT("SWG", "FlightThrottleReadout", "Throttle {0}%"), FText::AsNumber(FMath::RoundToInt(Throttle * 100.f))));
	ShipThrottleGauge->SetPercent(Throttle);
	ShipControlsText->SetText(FlightInputType == ECommonInputType::Gamepad
		? NSLOCTEXT("SWG", "FlightPadControls", "Left stick: Pitch / Roll   Right stick: Camera\nRT / LT: Throttle   RB / LB: Yaw   R3: View   D-pad Up/Down: Zoom   Start: Leave seat")
		: NSLOCTEXT("SWG", "FlightKeyboardControls", "W / S: Throttle   A / D: Yaw   Up / Down: Pitch\nQ / E: Roll   F: View   Wheel: Zoom   X: Leave seat"));
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
	FlightInputType = InputType;
	UpdateFlightReadout();
	if (ActionBar)
	{
		ActionBar->SetGamepadLayout(InputType == ECommonInputType::Gamepad);
	}
}

bool USWGHudWidget::TriggerActionSlot(int32 SlotIndex)
{
	const USWGObjectGraphSubsystem* Graph = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	if (Graph && Graph->IsLocalPlayerPiloting()) return false;
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
