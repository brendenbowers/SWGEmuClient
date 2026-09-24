#pragma once

#include "CoreMinimal.h"
#include "SWGWindowWidget.h"
#include "SWGPlanetMapWindowWidget.generated.h"

class UButton;
class UTextBlock;
class USWGPlanetMapWidget;
class USWGWaypointSubsystem;
class USWGMapLocationSubsystem;
class USWGTreSubsystem;

/**
 * The planet map window: a USWGPlanetMapWidget showing the planet the player
 * is on, the player as a heading arrow, and their waypoints on it. Double-
 * clicking the ground drops a waypoint there (/waypoint X Y). WBP_PlanetMap
 * owns the layout; only MapView is required.
 */
UCLASS(Abstract)
class SWGUI_API USWGPlanetMapWindowWidget : public USWGWindowWidget
{
	GENERATED_BODY()

public:
	void SetControllerMode(bool bEnabled);

	/** Flies the view onto the player. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void CenterOnPlayer();

	/** Double-clicking the ground asks the server for a waypoint there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	bool bCreateWaypointOnDoubleClick = true;

	/** Opens framed on the player at FocusDistance rather than on the whole planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	bool bStartOnPlayer = true;

	/** Eye distance CenterOnPlayer, the opening view and waypoint clicks use, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	float FocusDistance = 1400.f;

	/** How far the player walks before the buildings shown around them are reloaded, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	float BuildingRefocusDistance = 500.f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGPlanetMapWidget> MapView;

	/** Optional: "Tatooine  -1,234  5,678". */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LocationText;

	/** Optional: the hint line ("Double-click to set a waypoint"). */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CenterButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ZoomInButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ZoomOutButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> OverviewButton;

	/** Optional: swaps this window for the holographic map. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> HologramButton;

private:
	/** Raw position and heading (degrees clockwise from north) of the local player. */
	bool GetPlayerPosition(FVector2D& OutPosition, float& OutHeading) const;
	void RefreshPlanet();
	void RefreshLocationMarkers();
	void HandleLocationsChanged(const FString& Planet);

	UFUNCTION() void RefreshWaypointMarkers();
	UFUNCTION() void HandleMarkerClicked(FName Layer, FName MarkerId);
	UFUNCTION() void HandleGroundDoubleClicked(FVector2D RawPosition);
	UFUNCTION() void HandleMapPressed();
	UFUNCTION() void HandleCenterClicked();
	UFUNCTION() void HandleZoomInClicked();
	UFUNCTION() void HandleZoomOutClicked();
	UFUNCTION() void HandleOverviewClicked();
	UFUNCTION() void HandleHologramClicked();

	UPROPERTY()
	TObjectPtr<USWGWaypointSubsystem> Waypoints;

	UPROPERTY()
	TObjectPtr<USWGMapLocationSubsystem> MapLocations;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> ButtonTextTints;

	/** Waypoint marker id -> raw position, for fly-to on click. */
	TMap<FName, FVector2D> WaypointPositions;
	TMap<FName, FVector2D> LocationPositions;
	FDelegateHandle LocationsChangedHandle;
	FVector2D LastLocationCenter = FVector2D::ZeroVector;
	int32 LastLocationDetail = -1;
	FString Planet;
	FVector2D BuildingFocus = FVector2D::ZeroVector;
	bool bControllerMode = false;
};
