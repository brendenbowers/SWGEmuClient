#include "SWGPlanetMapWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"

namespace
{
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
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Root;
		ViewImage = WidgetTree->ConstructWidget<UImage>();
		Root->AddChild(ViewImage);
		FillParent(ViewImage);
		MarkerCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Root->AddChild(MarkerCanvas);
		FillParent(MarkerCanvas);
	}
	if (!MarkerCanvas)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGPlanetMapWidget %s: its designer tree has no MarkerCanvas, so markers won't show"), *GetName());
	}
	if (ViewImage)
	{
		ViewImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (MarkerCanvas)
	{
		MarkerCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (!MarkerWidgetClass)
	{
		MarkerWidgetClass = USWGMapMarkerWidget::StaticClass();
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
		if (ViewImage)
		{
			ViewImage->SetBrushResourceObject(MapScene->GetRenderTarget());
		}
		bTerrainWasReady = false;
		if (!Planet.IsEmpty())
		{
			MapScene->ShowPlanet(Planet, BuildingFocusPoints);
		}
	}
}

void USWGPlanetMapWidget::NativeDestruct()
{
	if (ViewImage)
	{
		ViewImage->SetBrushResourceObject(nullptr);
	}
	MapScene.Reset();
	Super::NativeDestruct();
}

void USWGPlanetMapWidget::ShowPlanet(const FString& InPlanet, const TArray<FVector2D>& InBuildingFocusPoints)
{
	if (InPlanet != Planet)
	{
		Planet = InPlanet;
		bTerrainWasReady = false;
		ResetView();
	}
	BuildingFocusPoints = InBuildingFocusPoints;
	if (MapScene)
	{
		MapScene->ShowPlanet(Planet, BuildingFocusPoints);
	}
}

USWGPlanetMapWidget::FMarkerLayer& USWGPlanetMapWidget::FindOrAddLayer(FName LayerName)
{
	if (FMarkerLayer* Existing = Layers.FindByPredicate([LayerName](const FMarkerLayer& Candidate) { return Candidate.Name == LayerName; }))
	{
		return *Existing;
	}
	FMarkerLayer& Layer = Layers.AddDefaulted_GetRef();
	Layer.Name = LayerName;
	// Its own canvas per layer keeps the draw order fixed when one rebuilds.
	Layer.Panel = WidgetTree->ConstructWidget<UCanvasPanel>();
	Layer.Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (MarkerCanvas)
	{
		MarkerCanvas->AddChild(Layer.Panel);
		FillParent(Layer.Panel);
	}
	return Layer;
}

USWGMapMarkerWidget* USWGPlanetMapWidget::AddMarkerWidget(FMarkerLayer& Layer, const FSWGMapMarker& Marker)
{
	const TSubclassOf<USWGMapMarkerWidget>* LayerClass = LayerMarkerClasses.Find(Layer.Name);
	const TSubclassOf<USWGMapMarkerWidget> MarkerClass = LayerClass && *LayerClass ? *LayerClass : MarkerWidgetClass;
	USWGMapMarkerWidget* MarkerWidget = CreateWidget<USWGMapMarkerWidget>(this, MarkerClass ? MarkerClass.Get() : USWGMapMarkerWidget::StaticClass());
	MarkerWidget->LayerName = Layer.Name;
	MarkerWidget->Marker = Marker;
	MarkerWidget->OnClicked.AddUObject(this, &USWGPlanetMapWidget::HandleMarkerWidgetClicked);
	// Hidden until UpdateMarkerPositions has placed it.
	MarkerWidget->SetVisibility(ESlateVisibility::Collapsed);
	Layer.Panel->AddChild(MarkerWidget);
	if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(MarkerWidget->Slot))
	{
		MarkerSlot->SetAutoSize(true);
	}
	MarkerWidget->ApplyMarker(Marker);
	Layer.Widgets.Add(MarkerWidget);
	return MarkerWidget;
}

void USWGPlanetMapWidget::SetMarkers(FName LayerName, const TArray<FSWGMapMarker>& Markers)
{
	FMarkerLayer& Layer = FindOrAddLayer(LayerName);
	Layer.Panel->ClearChildren();
	Layer.Widgets.Reset();
	for (const FSWGMapMarker& Marker : Markers)
	{
		AddMarkerWidget(Layer, Marker);
	}
	UpdateMarkerPositions();
}

void USWGPlanetMapWidget::UpdateMarker(FName LayerName, const FSWGMapMarker& Marker)
{
	FMarkerLayer& Layer = FindOrAddLayer(LayerName);
	for (USWGMapMarkerWidget* MarkerWidget : Layer.Widgets)
	{
		if (MarkerWidget->Marker.Id == Marker.Id)
		{
			const bool bRestyle = !MarkerWidget->Marker.LooksLike(Marker);
			MarkerWidget->Marker = Marker;
			if (bRestyle)
			{
				MarkerWidget->ApplyMarker(Marker);
			}
			return;
		}
	}
	AddMarkerWidget(Layer, Marker);
}

void USWGPlanetMapWidget::RemoveMarker(FName LayerName, FName MarkerId)
{
	FMarkerLayer& Layer = FindOrAddLayer(LayerName);
	for (int32 Index = Layer.Widgets.Num() - 1; Index >= 0; --Index)
	{
		if (Layer.Widgets[Index]->Marker.Id == MarkerId)
		{
			Layer.Widgets[Index]->RemoveFromParent();
			Layer.Widgets.RemoveAt(Index);
		}
	}
}

void USWGPlanetMapWidget::ClearLayer(FName LayerName)
{
	SetMarkers(LayerName, {});
}

void USWGPlanetMapWidget::SetLayerMarkerClass(FName LayerName, TSubclassOf<USWGMapMarkerWidget> MarkerClass)
{
	LayerMarkerClasses.Add(LayerName, MarkerClass);
}

void USWGPlanetMapWidget::HandleMarkerWidgetClicked(USWGMapMarkerWidget* MarkerWidget)
{
	OnMarkerClicked.Broadcast(MarkerWidget->LayerName, MarkerWidget->Marker.Id);
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
	if (!bTerrainWasReady && MapScene->IsTerrainReady())
	{
		bTerrainWasReady = true;
		// The real map size is only known now; an untouched overview reframes to it.
		const float Overview = FSWGPlanetMapCamera::OverviewDistanceFor(MapScene->GetMapSize());
		const bool bWasOverview = GoalCamera.Distance >= GoalCamera.OverviewDistance * 0.99f;
		GoalCamera.OverviewDistance = Overview;
		Camera.OverviewDistance = Overview;
		if (bWasOverview)
		{
			GoalCamera.Distance = Overview;
			Camera.Distance = Overview;
		}
		ClampCamera(GoalCamera);
		ClampCamera(Camera);
		OnTerrainReady.Broadcast();
	}
}

bool USWGPlanetMapWidget::ProjectToLocal(FVector2D RawPosition, FVector2D& OutLocalPosition) const
{
	const FVector2D LocalSize = GetCachedGeometry().GetLocalSize();
	if (!MapScene || LocalSize.X < 1.f || LocalSize.Y < 1.f)
	{
		return false;
	}
	FVector2D Pixel;
	if (!MapScene->Project(FVector(RawPosition.X, RawPosition.Y, MapScene->GetGroundHeight(RawPosition)), Pixel))
	{
		return false;
	}
	const FIntPoint Viewport = MapScene->GetViewportSize();
	OutLocalPosition = Pixel * LocalSize / FVector2D(Viewport.X, Viewport.Y);
	return OutLocalPosition.X >= 0.f && OutLocalPosition.Y >= 0.f && OutLocalPosition.X <= LocalSize.X && OutLocalPosition.Y <= LocalSize.Y;
}

void USWGPlanetMapWidget::UpdateMarkerPositions()
{
	for (FMarkerLayer& Layer : Layers)
	{
		for (USWGMapMarkerWidget* MarkerWidget : Layer.Widgets)
		{
			FVector2D Local;
			const bool bOnMap = ProjectToLocal(MarkerWidget->Marker.Position, Local);
			MarkerWidget->SetVisibility(bOnMap ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			if (!bOnMap)
			{
				continue;
			}
			if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(MarkerWidget->Slot))
			{
				MarkerSlot->SetAlignment(MarkerWidget->Anchor);
				MarkerSlot->SetPosition(Local);
			}
			if (MarkerWidget->Marker.bHasHeading)
			{
				MarkerWidget->ApplyScreenHeading(MarkerWidget->Marker.Heading - Camera.Yaw);
			}
		}
	}
}

void USWGPlanetMapWidget::ClampCamera(FSWGPlanetMapCamera& InOutCamera) const
{
	const float HalfMap = MapScene ? MapScene->GetMapSize() * 0.5f : 8192.f;
	InOutCamera.Target.X = FMath::Clamp(InOutCamera.Target.X, -HalfMap, HalfMap);
	InOutCamera.Target.Y = FMath::Clamp(InOutCamera.Target.Y, -HalfMap, HalfMap);
	InOutCamera.Distance = FMath::Clamp(InOutCamera.Distance, FSWGPlanetMapCamera::MinDistance, InOutCamera.OverviewDistance);
	InOutCamera.Tilt = FMath::Clamp(InOutCamera.Tilt, -MaxTilt, MaxTilt);
}

void USWGPlanetMapWidget::FlyTo(FVector2D Point, float Distance)
{
	GoalCamera.Target = Point;
	if (Distance > 0.f)
	{
		GoalCamera.Distance = FMath::Min(GoalCamera.Distance, Distance);
	}
	ClampCamera(GoalCamera);
}

void USWGPlanetMapWidget::JumpTo(FVector2D Point, float Distance)
{
	FlyTo(Point, Distance);
	Camera = GoalCamera;
}

void USWGPlanetMapWidget::ZoomBy(float Factor)
{
	ZoomAbout(Factor, nullptr);
}

void USWGPlanetMapWidget::Orbit(float DeltaYaw, float DeltaTilt)
{
	GoalCamera.Yaw = FRotator::NormalizeAxis(GoalCamera.Yaw + DeltaYaw);
	GoalCamera.Tilt += DeltaTilt;
	ClampCamera(GoalCamera);
}

void USWGPlanetMapWidget::ResetView()
{
	GoalCamera = FSWGPlanetMapCamera();
	GoalCamera.OverviewDistance = FSWGPlanetMapCamera::OverviewDistanceFor(MapScene ? MapScene->GetMapSize() : 16384.f);
	GoalCamera.Distance = GoalCamera.OverviewDistance;
	Camera = GoalCamera;
}

void USWGPlanetMapWidget::ZoomAbout(float Factor, const FVector2D* ScreenPosition)
{
	const float NewDistance = FMath::Clamp(GoalCamera.Distance * Factor, FSWGPlanetMapCamera::MinDistance, GoalCamera.OverviewDistance);
	FVector2D Anchor;
	if (ScreenPosition && ScreenToGround(*ScreenPosition, Anchor))
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

bool USWGPlanetMapWidget::ScreenToGround(const FVector2D& ScreenPosition, FVector2D& OutRawPosition) const
{
	FVector2D Pixel;
	return ScreenToPixel(ScreenPosition, Pixel) && MapScene->Deproject(Pixel, OutRawPosition);
}

bool USWGPlanetMapWidget::HandleControllerKey(FKey Key)
{
	if (Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder)
	{
		ZoomBy(Key == EKeys::Gamepad_RightShoulder ? 0.5f : 2.f);
		return true;
	}
	if (Key == EKeys::Gamepad_RightStick_Left || Key == EKeys::Gamepad_RightStick_Right)
	{
		Orbit(Key == EKeys::Gamepad_RightStick_Right ? 15.f : -15.f, 0.f);
		return true;
	}
	if (Key == EKeys::Gamepad_RightStick_Up || Key == EKeys::Gamepad_RightStick_Down)
	{
		Orbit(0.f, Key == EKeys::Gamepad_RightStick_Up ? 8.f : -8.f);
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
	bDragged = false;
	DragDistance = 0.f;
	DragLocal = GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply USWGPlanetMapWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	FVector2D Ground;
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && ScreenToGround(InMouseEvent.GetScreenSpacePosition(), Ground))
	{
		OnGroundDoubleClicked.Broadcast(Ground);
	}
	return FReply::Handled();
}

FReply USWGPlanetMapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if ((!bPanning && !bOrbiting) || !MapScene)
	{
		return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
	}
	// Raw deltas, not positions: while a drag has the mouse captured the in-world
	// input mode hides and locks the cursor, so its position stops changing.
	const FGeometry& Geometry = GetCachedGeometry();
	const FVector2D LocalDelta = InMouseEvent.GetCursorDelta() / FMath::Max(Geometry.Scale, KINDA_SMALL_NUMBER);
	DragDistance += LocalDelta.Size();
	if (!bDragged && DragDistance < ClickSlopPixels)
	{
		return FReply::Handled();
	}
	bDragged = true;
	if (bOrbiting)
	{
		Camera.Yaw = FRotator::NormalizeAxis(Camera.Yaw + LocalDelta.X * OrbitDegreesPerPixel);
		Camera.Tilt -= LocalDelta.Y * TiltDegreesPerPixel;
	}
	else
	{
		// Grab the ground: the point under the cursor follows it. Unclipped,
		// since a drag may leave the map while the mouse is captured.
		const FIntPoint Viewport = MapScene->GetViewportSize();
		const FVector2D LocalToPixel = FVector2D(Viewport.X, Viewport.Y) / Geometry.GetLocalSize();
		const FVector2D Next = DragLocal + LocalDelta;
		FVector2D Before;
		FVector2D After;
		if (MapScene->Deproject(DragLocal * LocalToPixel, Before) && MapScene->Deproject(Next * LocalToPixel, After))
		{
			Camera.Target += Before - After;
		}
		DragLocal = Next;
	}
	ClampCamera(Camera);
	// Direct manipulation cancels any fly-to in progress.
	GoalCamera = Camera;
	MapScene->SetCamera(Camera);
	return FReply::Handled();
}

FReply USWGPlanetMapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bPanning && !bOrbiting)
	{
		return FReply::Handled();
	}
	FVector2D Ground;
	if (bPanning && !bDragged && ScreenToGround(InMouseEvent.GetScreenSpacePosition(), Ground))
	{
		OnGroundClicked.Broadcast(Ground);
	}
	bPanning = false;
	bOrbiting = false;
	return FReply::Handled().ReleaseMouseCapture();
}
