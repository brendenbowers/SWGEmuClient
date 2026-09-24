#include "SWGHoloMapWidget.h"
#include "SWGHoloMapActor.h"
#include "SWGGameLayout.h"
#include "SWGMapMarkers.h"
#include "SWGRetailStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Common/SWGWorldScale.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGWaypointSubsystem.h"

namespace
{
	const FName PlayerLayer(TEXT("Player"));
	const FName WaypointLayer(TEXT("Waypoints"));
	constexpr float TurnDegreesPerPixel = 0.3f;
	constexpr float WheelZoomFactor = 0.8f;
	/** Fraction of the radius one D-pad pan step moves. */
	constexpr float GamepadPanStep = 0.15f;
	constexpr float AnalogDeadZone = 0.2f;
	/** Radii per second at full stick. */
	constexpr float AnalogPanSpeed = 0.9f;
	constexpr float AnalogTurnSpeed = 90.f;
	/** Log-radius per second at full trigger. */
	constexpr float AnalogZoomSpeed = 1.2f;
	const FLinearColor HintColor = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));

	UButton* MakeHintButton(UWidgetTree* Tree, UHorizontalBox* Row, const FText& Label)
	{
		UButton* Button = Tree->ConstructWidget<UButton>();
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetText(Label);
		Button->AddChild(Text);
		if (UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(Button))
		{
			ButtonSlot->SetPadding(FMargin(6.f, 0.f));
		}
		return Button;
	}
}

void USWGHoloMapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Root;
		UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>();
		Root->AddChild(Bar);
		if (UCanvasPanelSlot* BarSlot = Cast<UCanvasPanelSlot>(Bar->Slot))
		{
			BarSlot->SetAnchors(FAnchors(0.5f, 1.f));
			BarSlot->SetAlignment(FVector2D(0.5f, 1.f));
			BarSlot->SetPosition(FVector2D(0.f, -40.f));
			BarSlot->SetAutoSize(true);
		}
		HintText = WidgetTree->ConstructWidget<UTextBlock>();
		HintText->SetFont(SWGRetailStyle::Font(13));
		HintText->SetColorAndOpacity(FSlateColor(HintColor));
		HintText->SetShadowOffset(FVector2D(1.f, 1.f));
		if (UHorizontalBoxSlot* HintSlot = Bar->AddChildToHorizontalBox(HintText))
		{
			HintSlot->SetVerticalAlignment(VAlign_Center);
			HintSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		}
		CenterButton = MakeHintButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloCenter", "Center"));
		WindowButton = MakeHintButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloWindow", "Window Map"));
		CloseButton = MakeHintButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloClose", "Close"));
	}
	// The whole screen is the hologram's control surface.
	SetVisibility(ESlateVisibility::Visible);
	SetIsFocusable(true);
}

void USWGHoloMapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UGameInstance* GameInstance = GetGameInstance();
	Waypoints = GameInstance ? GameInstance->GetSubsystem<USWGWaypointSubsystem>() : nullptr;
	USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	if (Waypoints)
	{
		Waypoints->OnWaypointListChanged.AddUniqueDynamic(this, &USWGHoloMapWidget::RefreshWaypoints);
	}
	if (CenterButton) { CenterButton->OnClicked.AddUniqueDynamic(this, &USWGHoloMapWidget::HandleCenterClicked); }
	if (WindowButton) { WindowButton->OnClicked.AddUniqueDynamic(this, &USWGHoloMapWidget::HandleWindowClicked); }
	if (CloseButton) { CloseButton->OnClicked.AddUniqueDynamic(this, &USWGHoloMapWidget::HandleCloseClicked); }
	if (ButtonTextTints.IsEmpty())
	{
		for (UButton* Button : { CenterButton.Get(), WindowButton.Get(), CloseButton.Get() })
		{
			if (UObject* TextTint = SWGRetailStyle::ApplyHudButton(Button, Tre))
			{
				ButtonTextTints.Add(TextTint);
			}
		}
	}
	if (HintText)
	{
		HintText->SetText(bCreateWaypointOnDoubleClick
			? NSLOCTEXT("SWGEmu", "HoloHint", "Drag to pan  •  Right-drag to turn  •  Wheel to zoom  •  Double-click for a waypoint")
			: NSLOCTEXT("SWGEmu", "HoloHintNoWaypoint", "Drag to pan  •  Right-drag to turn  •  Wheel to zoom"));
	}
	Project();
	RefreshWaypoints();
	// Keyboard and gamepad both come here, so pad buttons don't also fire the action bar.
	SetFocus();
	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		SetUserFocus(PlayerController);
	}
}

void USWGHoloMapWidget::NativeDestruct()
{
	if (Waypoints)
	{
		Waypoints->OnWaypointListChanged.RemoveDynamic(this, &USWGHoloMapWidget::RefreshWaypoints);
	}
	RestoreView();
	Super::NativeDestruct();
}

FString USWGHoloMapWidget::GetPlanetName() const
{
	const USWGTerrainSubsystem* Terrain = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
	return Terrain ? Terrain->GetActivePlanetName() : FString();
}

void USWGHoloMapWidget::Project()
{
	APlayerController* PlayerController = GetOwningPlayer();
	APawn* Pawn = GetOwningPlayerPawn();
	UWorld* World = GetWorld();
	if (!PlayerController || !Pawn || !World)
	{
		return;
	}
	const FRotator Facing(0.f, Pawn->GetActorRotation().Yaw, 0.f);
	// Stand the projector on the ground in front, at table height; the pawn's own
	// origin isn't a reliable ground reference (it sits at the feet here).
	FVector ProjectorLocation = Pawn->GetActorLocation() + Facing.Vector() * ProjectorDistance;
	FHitResult Ground;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SWGHoloMapGround), false, Pawn);
	if (World->LineTraceSingleByChannel(Ground, ProjectorLocation + FVector(0.f, 0.f, 200.f), ProjectorLocation - FVector(0.f, 0.f, 500.f), ECC_Visibility, Query))
	{
		ProjectorLocation.Z = Ground.ImpactPoint.Z;
	}
	ProjectorLocation.Z += ProjectorHeight;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	// World-aligned, not turned to the player: north on the hologram is north outside.
	Hologram = World->SpawnActor<ASWGHoloMapActor>(ASWGHoloMapActor::StaticClass(), ProjectorLocation, FRotator::ZeroRotator, Params);
	if (!Hologram)
	{
		return;
	}
	FSWGMapMarker Self;
	SWGMapMarkers::MakePlayerMarker(Pawn, Self);
	Hologram->SetViewRadius(StartRadius);
	Hologram->SetViewCenter(Self.Position);
	// The projector droid hovers over the far side, out of the camera's way.
	Hologram->SetDroidSide(FVector2D(Facing.Vector()));

	const FVector CameraLocation = ProjectorLocation + Facing.RotateVector(CameraOffset);
	ShoulderCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CameraLocation,
		(Hologram->GetFocusLocation() - CameraLocation).Rotation(), Params);
	if (ShoulderCamera)
	{
		ShoulderCamera->GetCameraComponent()->SetFieldOfView(70.f);
		ShoulderCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		PreviousViewTarget = PlayerController->GetViewTarget();
		PlayerController->SetViewTargetWithBlend(ShoulderCamera, CameraBlendSeconds, VTBlend_EaseInOut, 2.f);
	}
	PlayerController->SetIgnoreMoveInput(true);
	PlayerController->SetIgnoreLookInput(true);
	// From over the shoulder the player's own name hangs right across the view.
	if (USWGTangibleComponent* Tangible = Pawn->FindComponentByClass<USWGTangibleComponent>())
	{
		Tangible->SetNameLabelHidden(true);
	}
	// The HUD fades back so the hologram reads; this overlay's own controls stay solid.
	if (USWGGameLayout* Layout = USWGGameLayout::GetLayout(this))
	{
		PreviousHudOpacity = Layout->GetRenderOpacity();
		Layout->SetRenderOpacity(HudOpacity);
	}
}

void USWGHoloMapWidget::RestoreView()
{
	if (bClosing)
	{
		return;
	}
	bClosing = true;
	APlayerController* PlayerController = GetOwningPlayer();
	if (PlayerController)
	{
		AActor* Target = PreviousViewTarget ? PreviousViewTarget.Get() : PlayerController->GetPawn();
		if (Target && ShoulderCamera)
		{
			PlayerController->SetViewTargetWithBlend(Target, CameraBlendSeconds, VTBlend_EaseInOut, 2.f);
		}
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
		if (USWGTangibleComponent* Tangible = PlayerController->GetPawn() ? PlayerController->GetPawn()->FindComponentByClass<USWGTangibleComponent>() : nullptr)
		{
			Tangible->SetNameLabelHidden(false);
		}
	}
	if (USWGGameLayout* Layout = USWGGameLayout::GetLayout(this))
	{
		Layout->SetRenderOpacity(PreviousHudOpacity);
	}
	if (ShoulderCamera)
	{
		// Kept until the blend back has left it.
		ShoulderCamera->SetLifeSpan(CameraBlendSeconds + 0.2f);
		ShoulderCamera = nullptr;
	}
	if (Hologram)
	{
		Hologram->Destroy();
		Hologram = nullptr;
	}
}

void USWGHoloMapWidget::Close()
{
	RestoreView();
	RemoveFromParent();
	OnClosed.Broadcast();
}

void USWGHoloMapWidget::SwitchToWindow()
{
	OnSwitchToWindow.Broadcast();
}

void USWGHoloMapWidget::CenterOnPlayer()
{
	FSWGMapMarker Self;
	if (Hologram && SWGMapMarkers::MakePlayerMarker(GetOwningPlayerPawn(), Self))
	{
		Hologram->SetViewCenter(Self.Position);
	}
}

void USWGHoloMapWidget::RefreshWaypoints()
{
	if (Hologram)
	{
		Hologram->SetMarkers(WaypointLayer, SWGMapMarkers::MakeWaypointMarkers(Waypoints, GetPlanetName()));
	}
}

void USWGHoloMapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!Hologram)
	{
		return;
	}
	ApplyAnalog(InDeltaTime);
	FSWGMapMarker Self;
	if (SWGMapMarkers::MakePlayerMarker(GetOwningPlayerPawn(), Self))
	{
		Hologram->SetMarkers(PlayerLayer, { Self });
	}
	if (ShoulderCamera)
	{
		// The terrain's height under the centre changes as it rebakes; keep looking at it.
		ShoulderCamera->SetActorRotation((Hologram->GetFocusLocation() - ShoulderCamera->GetActorLocation()).Rotation());
	}
}

bool USWGHoloMapWidget::MouseToRaw(FVector2D& OutRaw) const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	float MouseX = 0.f;
	float MouseY = 0.f;
	return PlayerController && PlayerController->GetMousePosition(MouseX, MouseY) && ScreenToRaw(FVector2D(MouseX, MouseY), OutRaw);
}

bool USWGHoloMapWidget::ScreenToRaw(const FVector2D& ViewportPosition, FVector2D& OutRaw) const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	FVector Origin;
	FVector Direction;
	return Hologram && PlayerController
		&& PlayerController->DeprojectScreenPositionToWorld(ViewportPosition.X, ViewportPosition.Y, Origin, Direction)
		&& Hologram->RayToRaw(Origin, Direction, OutRaw);
}

void USWGHoloMapWidget::Zoom(float Factor)
{
	if (Hologram)
	{
		Hologram->SetViewRadius(Hologram->GetViewRadius() * Factor);
	}
}

FReply USWGHoloMapWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Zoom(InMouseEvent.GetWheelDelta() > 0.f ? WheelZoomFactor : 1.f / WheelZoomFactor);
	return FReply::Handled();
}

FReply USWGHoloMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FKey Button = InMouseEvent.GetEffectingButton();
	bTurning = Button == EKeys::RightMouseButton || (Button == EKeys::LeftMouseButton && InMouseEvent.IsControlDown());
	bPanning = !bTurning && Button == EKeys::LeftMouseButton;
	// Tracked from deltas: while the mouse is captured the in-world input mode
	// hides and locks the cursor, so its reported position stops moving.
	float MouseX = 0.f;
	float MouseY = 0.f;
	const APlayerController* PlayerController = GetOwningPlayer();
	bHasVirtualCursor = PlayerController && PlayerController->GetMousePosition(MouseX, MouseY);
	VirtualCursor = FVector2D(MouseX, MouseY);
	return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

FReply USWGHoloMapWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	FVector2D Raw;
	USWGCommandSubsystem* Commands = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGCommandSubsystem>() : nullptr;
	if (bCreateWaypointOnDoubleClick && Commands && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && MouseToRaw(Raw))
	{
		// Core3 WaypointCommand, ground usage: "/waypoint X Y".
		Commands->SendCommand(TEXT("waypoint"), 0, FString::Printf(TEXT("%d %d"), FMath::RoundToInt(Raw.X), FMath::RoundToInt(Raw.Y)));
	}
	return FReply::Handled();
}

FReply USWGHoloMapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!Hologram || (!bPanning && !bTurning))
	{
		return FReply::Handled();
	}
	const FVector2D Delta = InMouseEvent.GetCursorDelta();
	if (bTurning)
	{
		Hologram->SetViewYaw(Hologram->GetViewYaw() + Delta.X * TurnDegreesPerPixel);
	}
	else if (bHasVirtualCursor)
	{
		// Grab the ground: the point that was under the cursor follows it.
		const APlayerController* PlayerController = GetOwningPlayer();
		int32 ViewportWidth = 0;
		int32 ViewportHeight = 0;
		if (PlayerController)
		{
			PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
		}
		const FVector2D Next(FMath::Clamp(VirtualCursor.X + Delta.X, 0.f, (float)ViewportWidth), FMath::Clamp(VirtualCursor.Y + Delta.Y, 0.f, (float)ViewportHeight));
		FVector2D Before;
		FVector2D After;
		if (ScreenToRaw(VirtualCursor, Before) && ScreenToRaw(Next, After))
		{
			Hologram->SetViewCenter(Hologram->GetViewCenter() + Before - After);
		}
		VirtualCursor = Next;
	}
	return FReply::Handled();
}

FReply USWGHoloMapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	bPanning = false;
	bTurning = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply USWGHoloMapWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
	{
		Close();
		return FReply::Handled();
	}
	if (!Hologram)
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}
	if (Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder)
	{
		Zoom(Key == EKeys::Gamepad_RightShoulder ? 0.5f : 2.f);
		return FReply::Handled();
	}
	// The sticks are read as analog (NativeOnAnalogValueChanged); swallow their
	// digital echoes so they don't step the view as well, or reach the game.
	if (Key.IsAnalog() || Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Gamepad_LeftStick_Down
		|| Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::Gamepad_LeftStick_Right
		|| Key == EKeys::Gamepad_RightStick_Up || Key == EKeys::Gamepad_RightStick_Down
		|| Key == EKeys::Gamepad_RightStick_Left || Key == EKeys::Gamepad_RightStick_Right
		|| Key == EKeys::Gamepad_LeftTrigger || Key == EKeys::Gamepad_RightTrigger)
	{
		return FReply::Handled();
	}
	const TMap<FKey, FVector2D> PanKeys = {
		{ EKeys::Gamepad_DPad_Up, FVector2D(0.f, 1.f) }, { EKeys::Gamepad_DPad_Down, FVector2D(0.f, -1.f) },
		{ EKeys::Gamepad_DPad_Right, FVector2D(1.f, 0.f) }, { EKeys::Gamepad_DPad_Left, FVector2D(-1.f, 0.f) },
	};
	if (const FVector2D* Step = PanKeys.Find(Key))
	{
		// Away from the camera is "up": the camera's yaw, less the content's own turn.
		const float CameraYaw = ShoulderCamera ? ShoulderCamera->GetActorRotation().Yaw : 0.f;
		const float Heading = FMath::DegreesToRadians(CameraYaw - Hologram->GetViewYaw());
		const FVector2D North(FMath::Sin(Heading), FMath::Cos(Heading));
		const FVector2D East(North.Y, -North.X);
		const FVector2D Move = (North * Step->Y + East * Step->X) * Hologram->GetViewRadius() * GamepadPanStep;
		Hologram->SetViewCenter(Hologram->GetViewCenter() + Move);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Left)
	{
		CenterOnPlayer();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Top)
	{
		SwitchToWindow();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply USWGHoloMapWidget::NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent)
{
	const FKey Key = InAnalogEvent.GetKey();
	const float Value = InAnalogEvent.GetAnalogValue();
	if (Key == EKeys::Gamepad_LeftX) { LeftStick.X = Value; }
	else if (Key == EKeys::Gamepad_LeftY) { LeftStick.Y = Value; }
	else if (Key == EKeys::Gamepad_RightX) { RightStick.X = Value; }
	else if (Key == EKeys::Gamepad_RightY) { RightStick.Y = Value; }
	else if (Key == EKeys::Gamepad_LeftTriggerAxis) { LeftTrigger = Value; }
	else if (Key == EKeys::Gamepad_RightTriggerAxis) { RightTrigger = Value; }
	else
	{
		return Super::NativeOnAnalogValueChanged(InGeometry, InAnalogEvent);
	}
	// Held here, so the pawn's own stick bindings don't walk or turn it meanwhile.
	return FReply::Handled();
}

void USWGHoloMapWidget::ApplyAnalog(float DeltaSeconds)
{
	auto DeadZone = [](float Value) { return FMath::Abs(Value) < AnalogDeadZone ? 0.f : Value; };
	const FVector2D Pan(DeadZone(LeftStick.X), DeadZone(LeftStick.Y));
	const float Turn = DeadZone(RightStick.X);
	const float Zoom = DeadZone(RightTrigger) - DeadZone(LeftTrigger);
	if (!Pan.IsZero())
	{
		// Stick up pans away from the camera, the same frame the D-pad uses.
		const float CameraYaw = ShoulderCamera ? ShoulderCamera->GetActorRotation().Yaw : 0.f;
		const float Heading = FMath::DegreesToRadians(CameraYaw - Hologram->GetViewYaw());
		const FVector2D North(FMath::Sin(Heading), FMath::Cos(Heading));
		const FVector2D East(North.Y, -North.X);
		Hologram->SetViewCenter(Hologram->GetViewCenter()
			+ (North * Pan.Y + East * Pan.X) * Hologram->GetViewRadius() * AnalogPanSpeed * DeltaSeconds);
	}
	if (Turn != 0.f)
	{
		Hologram->SetViewYaw(Hologram->GetViewYaw() + Turn * AnalogTurnSpeed * DeltaSeconds);
	}
	if (Zoom != 0.f)
	{
		// Right trigger closes in, left pulls back; exponential so it feels even at any range.
		Hologram->SetViewRadius(Hologram->GetViewRadius() * FMath::Exp(-Zoom * AnalogZoomSpeed * DeltaSeconds));
	}
}

void USWGHoloMapWidget::HandleCenterClicked() { CenterOnPlayer(); }
void USWGHoloMapWidget::HandleWindowClicked() { SwitchToWindow(); }
void USWGHoloMapWidget::HandleCloseClicked() { Close(); }
