#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Map/SWGPlanetMapScene.h"
#include "SWGPlanetMapWidget.generated.h"

class UButton;
class UCanvasPanel;
class UImage;

/** One pin on the map. Raw space position (metres, x east, y north). */
USTRUCT(BlueprintType)
struct SWGUI_API FSWGMapMarker
{
	GENERATED_BODY()

	/** Reported back by OnMarkerClicked; unique within its layer. */
	UPROPERTY(BlueprintReadWrite, Category = "SWGEmu|Map")
	FName Id;

	UPROPERTY(BlueprintReadWrite, Category = "SWGEmu|Map")
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "SWGEmu|Map")
	FText Label;

	/** Retail's travel-point label green unless set. */
	UPROPERTY(BlueprintReadWrite, Category = "SWGEmu|Map")
	FLinearColor LabelColor = FLinearColor::FromSRGBColor(FColor(0x62, 0xFF, 0x15));

	/** Drawn in retail's activated colour instead of its idle one. */
	UPROPERTY(BlueprintReadWrite, Category = "SWGEmu|Map")
	bool bSelected = false;

	/** Overrides the pin's idle colour (a waypoint's own colour); otherwise retail's orange. */
	UPROPERTY(BlueprintReadWrite, Category = "SWGEmu|Map")
	bool bCustomPinColor = false;

	UPROPERTY(BlueprintReadWrite, Category = "SWGEmu|Map")
	FLinearColor PinColor = FLinearColor::White;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FSWGOnMapMarkerClicked, FName /*Layer*/, FName /*MarkerId*/);

/**
 * Self-contained 3D planet map: renders FSWGPlanetMapScene, drives a
 * Google-Maps-style camera (left-drag pans, right/ctrl-drag orbits and tilts,
 * wheel zooms toward the cursor, FlyTo animates), and pins markers over it
 * in named layers so several sources (travel points, waypoints, ...) can
 * feed one map independently. Builds its own widget tree, so it can be
 * created from C++ and dropped into any window's panel.
 */
UCLASS()
class SWGUI_API USWGPlanetMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Loads Planet; buildings are shown around BuildingFocusPoints. Resets the view when the planet changes. */
	void ShowPlanet(const FString& Planet, const TArray<FVector2D>& BuildingFocusPoints);

	/** Replaces one layer's markers. Layers draw in the order they were first set; later ones on top. */
	void SetMarkers(FName Layer, const TArray<FSWGMapMarker>& Markers);

	/** Animates the camera onto Point, closing to Distance metres if it is further out. */
	void FlyTo(const FVector2D& Point, float Distance);

	/** Animated; Factor < 1 zooms in. */
	void ZoomBy(float Factor);

	/** Whole-planet overview, looking straight down. */
	void ResetView();

	/** LB/RB zoom and right-stick orbit/tilt. True if Key was consumed. */
	bool HandleControllerKey(const FKey& Key);

	bool IsTerrainReady() const { return MapScene && MapScene->IsTerrainReady(); }

	FSWGOnMapMarkerClicked OnMarkerClicked;

	/** Any press on the map, so a hosting window can raise itself. */
	FSimpleMulticastDelegate OnPressed;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	/** The widget tree owns Panel and Pins, which keeps them alive. */
	struct FMarkerLayer
	{
		FName Name;
		TObjectPtr<UCanvasPanel> Panel;
		TArray<FSWGMapMarker> Markers;
		/** One pin-plus-label box per marker, placed bottom-centre on its point. */
		TArray<TObjectPtr<UWidget>> Pins;
	};

	void RebuildLayer(FMarkerLayer& Layer);
	void UpdateMarkerPositions();
	void ZoomAbout(float Factor, const FVector2D* ScreenPosition);
	bool ScreenToPixel(const FVector2D& ScreenPosition, FVector2D& OutPixel) const;
	void ClampCamera(FSWGPlanetMapCamera& InOutCamera) const;

	UPROPERTY()
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY()
	TObjectPtr<UImage> ViewImage;

	/** Keyed by layer so rebuilding one layer drops only its forwarders. */
	UPROPERTY()
	TMap<FName, TObjectPtr<class USWGMapMarkerClickForwarderSet>> ClickForwarders;

	TArray<FMarkerLayer> Layers;
	TSharedPtr<FSWGPlanetMapScene> MapScene;
	FString Planet;
	TArray<FVector2D> BuildingFocusPoints;
	/** What is drawn this frame, easing toward GoalCamera. */
	FSWGPlanetMapCamera Camera;
	FSWGPlanetMapCamera GoalCamera;
	FVector2D LastDragPosition = FVector2D::ZeroVector;
	bool bPanning = false;
	bool bOrbiting = false;
};

/** Per-marker click target; UButton's dynamic delegate does not carry the sender. */
UCLASS()
class USWGMapMarkerClickForwarder : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Action;
	UFUNCTION() void HandleClicked() { if (Action) { Action(); } }
};

UCLASS()
class USWGMapMarkerClickForwarderSet : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<USWGMapMarkerClickForwarder>> Forwarders;
};
