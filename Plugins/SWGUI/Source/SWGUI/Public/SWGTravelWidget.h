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

/**
 * Ticket-terminal window, adapted from retail's planet/location selectors.
 * It builds its small fixed layout in C++ so mouse and controller modes share
 * one source of truth: mouse uses the combo boxes; controller uses D-pad,
 * X for round trip, A to purchase and B to close.
 */
UCLASS()
class SWGUI_API USWGTravelWidget : public USWGWindowWidget
{
	GENERATED_BODY()

public:
	void SetControllerMode(bool bEnabled);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void BuildLayout();
	void RefreshTravelData();
	void ShowGalaxyMap();
	void ShowPlanetMap(const FString& Planet);
	void RebuildMap();
	void SelectPlanet(int32 Index);
	void SelectDestination(int32 Index);
	void CycleMapSelection(int32 Delta);
	void RefreshFare();
	void ApplyControllerMode();
	static FText PlanetDisplayName(const FString& Planet);
	FText LocationDisplayName(const FString& Location) const;

	UFUNCTION() void HandleGalaxyClicked();
	UFUNCTION() void HandleRoundTripChanged(bool bChecked);
	UFUNCTION() void HandlePurchaseClicked();
	UFUNCTION() void HandleTravelDataChanged();

	UPROPERTY()
	TObjectPtr<UCanvasPanel> MapCanvas;

	UPROPERTY()
	TObjectPtr<UImage> MapImage;

	UPROPERTY()
	TObjectPtr<UTextBlock> MapTitleText;

	UPROPERTY()
	TObjectPtr<UButton> GalaxyButton;

	UPROPERTY()
	TObjectPtr<UCheckBox> RoundTripCheck;

	UPROPERTY()
	TObjectPtr<UTextBlock> DepartureText;

	UPROPERTY()
	TObjectPtr<UTextBlock> FareText;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UTextBlock> ControllerHelpText;

	UPROPERTY()
	TObjectPtr<UButton> PurchaseButton;

	UPROPERTY()
	TObjectPtr<USWGTravelSubsystem> Travel;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TArray<TObjectPtr<class USWGTravelMapClickForwarder>> MapClickForwarders;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> MapButtons;

	TArray<FString> VisiblePlanets;
	TArray<FSWGTravelDestination> VisibleDestinations;
	FString SelectedPlanet;
	int32 SelectedPlanetIndex = 0;
	int32 SelectedDestinationIndex = INDEX_NONE;
	bool bGalaxyView = false;
	bool bControllerMode = false;
};

/** Per-marker click target; UButton's dynamic delegate does not carry the sender. */
UCLASS()
class USWGTravelMapClickForwarder : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Action;
	UFUNCTION() void HandleClicked() { if (Action) { Action(); } }
};
