#pragma once

#include "CoreMinimal.h"
#include "SWGWindowWidget.h"
#include "Subsystems/SWGTravelSubsystem.h"
#include "SWGTravelWidget.generated.h"

class UButton;
class UCanvasPanel;
class UCheckBox;
class UImage;
class UTextBlock;
class USWGTreSubsystem;
class USWGWaypointSubsystem;
class USWGPlanetMapWidget;

/**
 * Ticket-terminal window, adapted from retail's planet/location selectors.
 * WBP_Travel owns the fixed layout; C++ supplies live destinations and input.
 * The planet view is a USWGPlanetMapWidget placed in MapCanvas, fed the
 * travel points and the player's waypoints on that planet as two layers.
 */
UCLASS(Abstract)
class SWGUI_API USWGTravelWidget : public USWGWindowWidget
{
	GENERATED_BODY()

public:
	void SetControllerMode(bool bEnabled);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void RefreshTravelData();
	void ShowGalaxyMap();
	void ShowPlanetMap(const FString& Planet);
	void RebuildMap();
	void RefreshTravelMarkers();
	void SelectPlanet(int32 Index);
	void SelectDestination(int32 Index);
	void CycleMapSelection(int32 Delta);
	void RefreshFare();
	/** The local player's balances; false before its creature baseline arrives. */
	bool GetPlayerCredits(int32& OutCash, int32& OutBank) const;
	void ApplyControllerMode();
	void HandleMapMarkerClicked(FName Layer, FName MarkerId);
	static FText PlanetDisplayName(const FString& Planet);
	FText LocationDisplayName(const FString& Location) const;

	UFUNCTION() void HandleGalaxyClicked();
	UFUNCTION() void HandleRoundTripChanged(bool bChecked);
	UFUNCTION() void HandlePurchaseClicked();
	UFUNCTION() void HandleTravelDataChanged();
	UFUNCTION() void HandleZoomInClicked();
	UFUNCTION() void HandleZoomOutClicked();
	UFUNCTION() void RefreshWaypointMarkers();

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCanvasPanel> MapCanvas;

	/** Left over from the 2D map; hidden, the map widget draws its own view. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> MapImage;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> MapTitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> GalaxyButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCheckBox> RoundTripCheck;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> DepartureText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> FareText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> ControllerHelpText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> PurchaseButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> ZoomInButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> ZoomOutButton;

private:
	UPROPERTY()
	TObjectPtr<USWGTravelSubsystem> Travel;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TObjectPtr<USWGWaypointSubsystem> Waypoints;

	UPROPERTY()
	TObjectPtr<USWGPlanetMapWidget> MapView;

	UPROPERTY()
	TArray<TObjectPtr<class USWGTravelPlanetClickForwarder>> PlanetClickForwarders;

	/** Per-state label recolouring for the retail-styled buttons (SWGRetailStyle::ApplyHudButton). */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> ButtonTextTints;

	/** Galaxy view's planet buttons, removed from MapCanvas on each rebuild. */
	UPROPERTY()
	TArray<TObjectPtr<UButton>> PlanetButtons;

	TArray<FString> VisiblePlanets;
	TArray<FSWGTravelDestination> VisibleDestinations;
	/** Waypoint object id -> raw position, for fly-to on click. */
	TMap<FName, FVector2D> WaypointPositions;
	FString SelectedPlanet;
	/** Balances last shown, so a credit delta while the window is open refreshes the fare line. */
	int32 ShownCash = INDEX_NONE;
	int32 ShownBank = INDEX_NONE;
	int32 SelectedPlanetIndex = 0;
	int32 SelectedDestinationIndex = INDEX_NONE;
	bool bGalaxyView = false;
	bool bControllerMode = false;
	bool bTerrainWasReady = false;
};

/** Per-planet click target; UButton's dynamic delegate does not carry the sender. */
UCLASS()
class USWGTravelPlanetClickForwarder : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Action;
	UFUNCTION() void HandleClicked() { if (Action) { Action(); } }
};
