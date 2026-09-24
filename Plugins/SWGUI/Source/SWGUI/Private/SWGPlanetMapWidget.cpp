#include "SWGPlanetMapWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SWGRetailStyle.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	/** Per-second rate the drawn camera closes on the goal; higher is snappier. */
	constexpr float CameraEaseRate = 5.f;
	constexpr float OrbitDegreesPerPixel = 0.25f;
	constexpr float TiltDegreesPerPixel = 0.2f;
	constexpr float MaxTilt = 60.f;
	constexpr float WheelZoomFactor = 0.7f;

	void FillParent(UWidget* Widget)
	{
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
		{
			CanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			CanvasSlot->SetOffsets(FMargin(0.f));
		}
	}
}

void USWGPlanetMapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = RootCanvas;
		ViewImage = WidgetTree->ConstructWidget<UImage>();
		ViewImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		RootCanvas->AddChild(ViewImage);
		FillParent(ViewImage);
	}
	// The map itself takes the clicks for pan/orbit; only markers sit above it.
	SetVisibility(ESlateVisibility::Visible);
}

void USWGPlanetMapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!MapScene)
	{
		MapScene = MakeShared<FSWGPlanetMapScene>(GetGameInstance());
		ViewImage->SetBrushResourceObject(MapScene->GetRenderTarget());
		if (!Planet.IsEmpty())
		{
			MapScene->ShowPlanet(Planet, BuildingFocusPoints);
		}
	}
}

void USWGPlanetMapWidget::NativeDestruct()
{
	ViewImage->SetBrushResourceObject(nullptr);
	MapScene.Reset();
	Super::NativeDestruct();
}

void USWGPlanetMapWidget::ShowPlanet(const FString& InPlanet, const TArray<FVector2D>& InBuildingFocusPoints)
{
	if (InPlanet != Planet)
	{
		Planet = InPlanet;
		ResetView();
	}
	BuildingFocusPoints = InBuildingFocusPoints;
	if (MapScene)
	{
		MapScene->ShowPlanet(Planet, BuildingFocusPoints);
	}
}

void USWGPlanetMapWidget::SetMarkers(FName LayerName, const TArray<FSWGMapMarker>& Markers)
{
	FMarkerLayer* Layer = Layers.FindByPredicate([LayerName](const FMarkerLayer& Candidate) { return Candidate.Name == LayerName; });
	if (!Layer)
	{
		Layer = &Layers.AddDefaulted_GetRef();
		Layer->Name = LayerName;
		Layer->Panel = WidgetTree->ConstructWidget<UCanvasPanel>();
		Layer->Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		RootCanvas->AddChild(Layer->Panel);
		FillParent(Layer->Panel);
	}
	Layer->Markers = Markers;
	RebuildLayer(*Layer);
}

void USWGPlanetMapWidget::RebuildLayer(FMarkerLayer& Layer)
{
	Layer.Panel->ClearChildren();
	Layer.Pins.Reset();
	USWGMapMarkerClickForwarderSet* ForwarderSet = NewObject<USWGMapMarkerClickForwarderSet>(this);
	ClickForwarders.Add(Layer.Name, ForwarderSet);
	USWGTreSubsystem* Tre = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
	for (const FSWGMapMarker& Marker : Layer.Markers)
	{
		// Retail's travel map: the city pin with its name in green above it.
		UVerticalBox* Pin = WidgetTree->ConstructWidget<UVerticalBox>();
		// Hidden until UpdateMarkerPositions has placed it.
		Pin->SetVisibility(ESlateVisibility::Collapsed);

		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(Marker.Label);
		Label->SetColorAndOpacity(FSlateColor(Marker.LabelColor));
		Label->SetShadowOffset(FVector2D(1.f, 1.f));
		Label->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
		Label->SetFont(SWGRetailStyle::BoldFont(Marker.bSelected ? 13 : 11));
		Label->SetJustification(ETextJustify::Center);
		Label->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UVerticalBoxSlot* LabelSlot = Pin->AddChildToVerticalBox(Label))
		{
			LabelSlot->SetHorizontalAlignment(HAlign_Center);
		}

		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		const FLinearColor IdleColor = Marker.bSelected ? SWGRetailStyle::PinActivated
			: Marker.bCustomPinColor ? Marker.PinColor : SWGRetailStyle::PinDefault;
		Button->SetStyle(SWGRetailStyle::MakePinStyle(Tre, IdleColor));
		USizeBox* PinSize = WidgetTree->ConstructWidget<USizeBox>();
		PinSize->SetWidthOverride(SWGRetailStyle::PinSize.X);
		PinSize->SetHeightOverride(SWGRetailStyle::PinSize.Y);
		PinSize->AddChild(Button);
		if (UVerticalBoxSlot* ButtonSlot = Pin->AddChildToVerticalBox(PinSize))
		{
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
		}

		Layer.Panel->AddChild(Pin);
		if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(Pin->Slot))
		{
			MarkerSlot->SetAutoSize(true);
			// Bottom-centre on the point, like a pin.
			MarkerSlot->SetAlignment(FVector2D(0.5f, 1.f));
		}
		USWGMapMarkerClickForwarder* Forwarder = NewObject<USWGMapMarkerClickForwarder>(ForwarderSet);
		Forwarder->Action = [this, LayerName = Layer.Name, MarkerId = Marker.Id]() { OnMarkerClicked.Broadcast(LayerName, MarkerId); };
		Button->OnClicked.AddDynamic(Forwarder, &USWGMapMarkerClickForwarder::HandleClicked);
		ForwarderSet->Forwarders.Add(Forwarder);
		Layer.Pins.Add(Pin);
	}
	UpdateMarkerPositions();
}

void USWGPlanetMapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!MapScene)
	{
		return;
	}
	Camera = Camera.IsNearlyEqual(GoalCamera)
		? GoalCamera
		: FSWGPlanetMapCamera::Blend(Camera, GoalCamera, 1.f - FMath::Exp(-InDeltaTime * CameraEaseRate));
	const FVector2D PixelSize = MyGeometry.GetAbsoluteSize();
	if (PixelSize.X >= 1.f && PixelSize.Y >= 1.f)
	{
		MapScene->SetViewportSize(FIntPoint(FMath::RoundToInt(PixelSize.X), FMath::RoundToInt(PixelSize.Y)));
	}
	MapScene->SetCamera(Camera);
	MapScene->RenderIfDirty();
	UpdateMarkerPositions();
}

void USWGPlanetMapWidget::UpdateMarkerPositions()
{
	if (!MapScene)
	{
		return;
	}
	const FVector2D LocalSize = GetCachedGeometry().GetLocalSize();
	const FIntPoint Viewport = MapScene->GetViewportSize();
	if (LocalSize.X < 1.f || LocalSize.Y < 1.f)
	{
		return;
	}
	const FVector2D PixelToLocal = LocalSize / FVector2D(Viewport.X, Viewport.Y);
	for (FMarkerLayer& Layer : Layers)
	{
		for (int32 Index = 0; Index < Layer.Markers.Num(); ++Index)
		{
			const FVector2D& Position = Layer.Markers[Index].Position;
			FVector2D Pixel;
			const bool bInFront = MapScene->Project(FVector(Position.X, Position.Y, MapScene->GetGroundHeight(Position)), Pixel);
			const FVector2D Local = Pixel * PixelToLocal;
			const bool bOnMap = bInFront && Local.X >= 0.f && Local.Y >= 0.f && Local.X <= LocalSize.X && Local.Y <= LocalSize.Y;
			UWidget* Pin = Layer.Pins[Index];
			Pin->SetVisibility(bOnMap ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			if (bOnMap)
			{
				Cast<UCanvasPanelSlot>(Pin->Slot)->SetPosition(Local);
			}
		}
	}
}

void USWGPlanetMapWidget::ClampCamera(FSWGPlanetMapCamera& InOutCamera) const
{
	const float HalfMap = MapScene ? MapScene->GetMapSize() * 0.5f : 8192.f;
	InOutCamera.Target.X = FMath::Clamp(InOutCamera.Target.X, -HalfMap, HalfMap);
	InOutCamera.Target.Y = FMath::Clamp(InOutCamera.Target.Y, -HalfMap, HalfMap);
	InOutCamera.Distance = FMath::Clamp(InOutCamera.Distance, FSWGPlanetMapCamera::MinDistance, FSWGPlanetMapCamera::MaxDistance);
	InOutCamera.Tilt = FMath::Clamp(InOutCamera.Tilt, -MaxTilt, MaxTilt);
}

void USWGPlanetMapWidget::FlyTo(const FVector2D& Point, float Distance)
{
	GoalCamera.Target = Point;
	GoalCamera.Distance = FMath::Min(GoalCamera.Distance, Distance);
	ClampCamera(GoalCamera);
}

void USWGPlanetMapWidget::ZoomBy(float Factor)
{
	ZoomAbout(Factor, nullptr);
}

void USWGPlanetMapWidget::ResetView()
{
	GoalCamera = FSWGPlanetMapCamera();
	Camera = GoalCamera;
}

void USWGPlanetMapWidget::ZoomAbout(float Factor, const FVector2D* ScreenPosition)
{
	const float NewDistance = FMath::Clamp(GoalCamera.Distance * Factor, FSWGPlanetMapCamera::MinDistance, FSWGPlanetMapCamera::MaxDistance);
	FVector2D Pixel;
	FVector2D Anchor;
	if (ScreenPosition && ScreenToPixel(*ScreenPosition, Pixel) && MapScene->Deproject(Pixel, Anchor))
	{
		// Scaling the target about the ground point keeps that point under the cursor.
		GoalCamera.Target = Anchor + (GoalCamera.Target - Anchor) * (NewDistance / GoalCamera.Distance);
	}
	GoalCamera.Distance = NewDistance;
	ClampCamera(GoalCamera);
}

bool USWGPlanetMapWidget::ScreenToPixel(const FVector2D& ScreenPosition, FVector2D& OutPixel) const
{
	const FGeometry& Geometry = GetCachedGeometry();
	if (!MapScene || !Geometry.IsUnderLocation(ScreenPosition))
	{
		return false;
	}
	const FIntPoint Viewport = MapScene->GetViewportSize();
	OutPixel = Geometry.AbsoluteToLocal(ScreenPosition) / Geometry.GetLocalSize() * FVector2D(Viewport.X, Viewport.Y);
	return true;
}

bool USWGPlanetMapWidget::HandleControllerKey(const FKey& Key)
{
	if (Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder)
	{
		ZoomBy(Key == EKeys::Gamepad_RightShoulder ? 0.5f : 2.f);
		return true;
	}
	if (Key == EKeys::Gamepad_RightStick_Left || Key == EKeys::Gamepad_RightStick_Right)
	{
		GoalCamera.Yaw = FRotator::NormalizeAxis(GoalCamera.Yaw + (Key == EKeys::Gamepad_RightStick_Right ? 15.f : -15.f));
		return true;
	}
	if (Key == EKeys::Gamepad_RightStick_Up || Key == EKeys::Gamepad_RightStick_Down)
	{
		GoalCamera.Tilt += Key == EKeys::Gamepad_RightStick_Up ? 8.f : -8.f;
		ClampCamera(GoalCamera);
		return true;
	}
	return false;
}

FReply USWGPlanetMapWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FVector2D ScreenPosition = InMouseEvent.GetScreenSpacePosition();
	ZoomAbout(InMouseEvent.GetWheelDelta() > 0.f ? WheelZoomFactor : 1.f / WheelZoomFactor, &ScreenPosition);
	return FReply::Handled();
}

FReply USWGPlanetMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	OnPressed.Broadcast();
	const FKey Button = InMouseEvent.GetEffectingButton();
	if (Button != EKeys::LeftMouseButton && Button != EKeys::RightMouseButton)
	{
		return FReply::Handled();
	}
	// Ctrl+left orbits too, for trackpads.
	bOrbiting = Button == EKeys::RightMouseButton || InMouseEvent.IsControlDown();
	bPanning = !bOrbiting;
	LastDragPosition = InMouseEvent.GetScreenSpacePosition();
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply USWGPlanetMapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if ((!bPanning && !bOrbiting) || !MapScene)
	{
		return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
	}
	const FVector2D Position = InMouseEvent.GetScreenSpacePosition();
	const FGeometry& Geometry = GetCachedGeometry();
	if (bOrbiting)
	{
		const FVector2D Delta = Geometry.AbsoluteToLocal(Position) - Geometry.AbsoluteToLocal(LastDragPosition);
		Camera.Yaw = FRotator::NormalizeAxis(Camera.Yaw + Delta.X * OrbitDegreesPerPixel);
		Camera.Tilt -= Delta.Y * TiltDegreesPerPixel;
	}
	else
	{
		// Grab the ground: the point under the cursor follows it. Unclipped,
		// since a drag may leave the map while the mouse is captured.
		const FIntPoint Viewport = MapScene->GetViewportSize();
		const FVector2D LocalToPixel = FVector2D(Viewport.X, Viewport.Y) / Geometry.GetLocalSize();
		FVector2D Before;
		FVector2D After;
		if (MapScene->Deproject(Geometry.AbsoluteToLocal(LastDragPosition) * LocalToPixel, Before)
			&& MapScene->Deproject(Geometry.AbsoluteToLocal(Position) * LocalToPixel, After))
		{
			Camera.Target += Before - After;
		}
	}
	ClampCamera(Camera);
	// Direct manipulation cancels any fly-to in progress.
	GoalCamera = Camera;
	MapScene->SetCamera(Camera);
	LastDragPosition = Position;
	return FReply::Handled();
}

FReply USWGPlanetMapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bPanning || bOrbiting)
	{
		bPanning = false;
		bOrbiting = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Handled();
}
