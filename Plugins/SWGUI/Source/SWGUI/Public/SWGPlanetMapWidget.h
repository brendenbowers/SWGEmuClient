#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Map/SWGPlanetMapScene.h"
#include "SWGMapMarkerWidget.h"
#include "SWGPlanetMapWidget.generated.h"

class UCanvasPanel;
class UImage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSWGOnMapMarkerClicked, FName, Layer, FName, MarkerId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSWGOnMapGroundClicked, FVector2D, RawPosition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnMapEvent);

/**
 * Self-contained 3D planet map: renders FSWGPlanetMapScene, drives a
 * Google-Maps-style camera (left-drag pans, right/ctrl-drag orbits and tilts,
 * wheel zooms toward the cursor, FlyTo animates), and pins markers over it
 * in named layers so several sources (travel points, waypoints, the player)
 * feed one map independently.
 *
 * Drop it into any widget Blueprint. It builds its own view and marker
 * canvas; a Blueprint subclass may lay out its own tree instead, binding
 * ViewImage and MarkerCanvas. Marker looks come from MarkerWidgetClass (or a
 * per-layer class), so they are restyled in Blueprint too.
 */
UCLASS(Blueprintable)
class SWGUI_API USWGPlanetMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Loads Planet ("tatooine"); buildings are shown around BuildingFocusPoints. Resets the view when the planet changes. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void ShowPlanet(const FString& Planet, const TArray<FVector2D>& BuildingFocusPoints);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Map")
	const FString& GetPlanet() const { return Planet; }

	/** Replaces one layer's markers. Layers draw in the order first set; later ones on top. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void SetMarkers(FName Layer, const TArray<FSWGMapMarker>& Markers);

	/** Adds or updates one marker by Id; moving it costs nothing, restyling rebuilds only it. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void UpdateMarker(FName Layer, const FSWGMapMarker& Marker);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void RemoveMarker(FName Layer, FName MarkerId);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void ClearLayer(FName Layer);

	/** Marker look for one layer; takes effect on its next SetMarkers. None falls back to MarkerWidgetClass. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void SetLayerMarkerClass(FName Layer, TSubclassOf<USWGMapMarkerWidget> MarkerClass);

	/** Animates the camera onto Point, closing to Distance metres if it is further out (0 keeps the zoom). */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void FlyTo(FVector2D Point, float Distance = 0.f);

	/** FlyTo without the flight: the view is simply there. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void JumpTo(FVector2D Point, float Distance = 0.f);

	/** Animated; Factor < 1 zooms in. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void ZoomBy(float Factor);

	/** Animated turn and tilt, in degrees. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void Orbit(float DeltaYaw, float DeltaTilt);

	/** Whole-planet overview, looking straight down, north up. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void ResetView();

	/** LB/RB zoom and right-stick orbit/tilt. True if Key was consumed. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	bool HandleControllerKey(FKey Key);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Map")
	bool IsTerrainReady() const { return MapScene && MapScene->IsTerrainReady(); }

	/** Where the camera looks, raw metres. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Map")
	FVector2D GetViewTarget() const { return Camera.Target; }

	/** Raw point to this widget's local space; false when off the view or behind the camera. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Map")
	bool ProjectToLocal(FVector2D RawPosition, FVector2D& OutLocalPosition) const;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Map")
	FSWGOnMapMarkerClicked OnMarkerClicked;

	/** A left click on the ground that didn't drag. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Map")
	FSWGOnMapGroundClicked OnGroundClicked;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Map")
	FSWGOnMapGroundClicked OnGroundDoubleClicked;

	/** Any press on the map, so a hosting window can raise itself. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Map")
	FSWGOnMapEvent OnPressed;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Map")
	FSWGOnMapEvent OnTerrainReady;

	/** Marker look for every layer without its own class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	TSubclassOf<USWGMapMarkerWidget> MarkerWidgetClass;

	/** Per-second rate the drawn camera closes on its goal; higher is snappier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map|Camera", meta = (ClampMin = "0.5"))
	float CameraEaseRate = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map|Camera")
	float OrbitDegreesPerPixel = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map|Camera")
	float TiltDegreesPerPixel = 0.2f;

	/** How far the player may tilt away from the zoom-driven pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map|Camera", meta = (ClampMin = "0", ClampMax = "80"))
	float MaxTilt = 60.f;

	/** Distance multiplier per wheel notch toward the cursor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map|Camera", meta = (ClampMin = "0.1", ClampMax = "0.95"))
	float WheelZoomFactor = 0.7f;

	/** A press that moves less than this many pixels is a click, not a drag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map|Camera")
	float ClickSlopPixels = 4.f;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** Optional: where the 3D view is drawn. Built when the class has no designer tree. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Map", meta = (BindWidgetOptional))
	TObjectPtr<UImage> ViewImage;

	/** Optional: holds the marker layers, over the view. Built when the class has no designer tree. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Map", meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> MarkerCanvas;

private:
	/** The widget tree owns Panel and Widgets, which keeps them alive. */
	struct FMarkerLayer
	{
		FName Name;
		TObjectPtr<UCanvasPanel> Panel;
		TArray<TObjectPtr<USWGMapMarkerWidget>> Widgets;
	};

	FMarkerLayer& FindOrAddLayer(FName LayerName);
	USWGMapMarkerWidget* AddMarkerWidget(FMarkerLayer& Layer, const FSWGMapMarker& Marker);
	void HandleMarkerWidgetClicked(USWGMapMarkerWidget* MarkerWidget);
	void UpdateMarkerPositions();
	void ZoomAbout(float Factor, const FVector2D* ScreenPosition);
	bool ScreenToPixel(const FVector2D& ScreenPosition, FVector2D& OutPixel) const;
	bool ScreenToGround(const FVector2D& ScreenPosition, FVector2D& OutRawPosition) const;
	void ClampCamera(FSWGPlanetMapCamera& InOutCamera) const;

	UPROPERTY()
	TMap<FName, TSubclassOf<USWGMapMarkerWidget>> LayerMarkerClasses;

	TArray<FMarkerLayer> Layers;
	TSharedPtr<FSWGPlanetMapScene> MapScene;
	FString Planet;
	TArray<FVector2D> BuildingFocusPoints;
	/** What is drawn this frame, easing toward GoalCamera. */
	FSWGPlanetMapCamera Camera;
	FSWGPlanetMapCamera GoalCamera;
	/** The drag's cursor in local units, advanced by raw deltas (the cursor itself is locked while captured). */
	FVector2D DragLocal = FVector2D::ZeroVector;
	/** Total local distance moved since the press, against ClickSlopPixels. */
	float DragDistance = 0.f;
	bool bPanning = false;
	bool bOrbiting = false;
	bool bDragged = false;
	bool bTerrainWasReady = false;
};
