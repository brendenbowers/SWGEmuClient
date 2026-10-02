#pragma once

#include "CoreMinimal.h"
#include "SWGWindowWidget.h"
#include "SWGMapMarkerWidget.h"
#include "SWGSurveyWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;
class USWGPlanetMapWidget;
class USWGSurveySubsystem;
class USWGTreSubsystem;
class USWGWaypointSubsystem;

/**
 * A survey grid point on the map: its percentage in the density colour,
 * centred on the point. Style "Best" (the point the server's waypoint goes on)
 * is larger and ringed, as retail's bullseye was.
 */
UCLASS(Blueprintable)
class SWGUIWINDOW_API USWGSurveyMarkerWidget : public USWGMapMarkerWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void ApplyMarker_Implementation(const FSWGMapMarker& InMarker) override;
};

/**
 * The survey tool window, after retail's ui_res_survey.inc: the ground around
 * the player on the left with the last survey's concentrations laid over it,
 * the resources the tool can find on the right, and Sample / Survey below.
 * WBP_Survey owns the layout; C++ fills the list and drives the map.
 */
UCLASS(Abstract)
class SWGUIWINDOW_API USWGSurveyWidget : public USWGWindowWidget
{
	GENERATED_BODY()

public:
	/** Eye distance as a multiple of the survey range, so the whole grid fits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Survey")
	float ViewDistancePerRange = 2.2f;

	/** Marker look for the survey points; unset uses USWGSurveyMarkerWidget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Survey")
	TSubclassOf<USWGMapMarkerWidget> SurveyMarkerClass;

	/** Fades the concentration field over the terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Survey", meta = (ClampMin = "0", ClampMax = "1"))
	float OverlayOpacity = 0.6f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGPlanetMapWidget> MapView;

	/** Filled with class headings and one button per resource. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> ResourceList;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> SurveyButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> SampleButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> RangeText;

	/** Optional: "Mineral Resources" over the list. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResourceTitleText;

	/** Optional: what the tool is doing, and the survey's outcome. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	/** Optional: opens the server's range list. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> RangeButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CenterButton;

	/** Optional: swaps this window for the survey hologram. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> HologramButton;

private:
	void RebuildResourceList();
	void RefreshSelection();
	void RefreshResult();
	void RefreshStatus();
	void RefreshWaypoints();
	void FrameSurveyArea(bool bJump);
	bool GetPlayerPosition(FVector2D& OutPosition) const;

	UFUNCTION() void HandleResourcesChanged();
	UFUNCTION() void HandleSurveyStateChanged();
	UFUNCTION() void HandleResultReceived();
	UFUNCTION() void HandleWaypointsChanged();
	UFUNCTION() void HandleSurveyClicked();
	UFUNCTION() void HandleSampleClicked();
	UFUNCTION() void HandleRangeClicked();
	UFUNCTION() void HandleCenterClicked();
	UFUNCTION() void HandleHologramClicked();
	UFUNCTION() void HandleMapPressed();

	UPROPERTY()
	TObjectPtr<USWGSurveySubsystem> Survey;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TObjectPtr<USWGWaypointSubsystem> Waypoints;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> ButtonTextTints;

	/** One per resource, in list order; clicks come back through the forwarders. */
	UPROPERTY()
	TArray<TObjectPtr<UButton>> ResourceButtons;

	UPROPERTY()
	TArray<TObjectPtr<class USWGTravelPlanetClickForwarder>> ResourceClickForwarders;

	TArray<FString> ResourceButtonNames;
	FString Planet;
	/** A local status line (sample sent, ...) that holds until the survey state changes. */
	FText TransientStatus;
};
