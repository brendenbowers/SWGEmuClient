#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "CommonInputTypeEnum.h"
#include "SWGUITypes.h"
#include "SWGWindowUISubsystem.generated.h"

class USWGUISubsystem;
class USWGWindowWidget;

/**
 * The window presentation of the UI features: floating windows stacked above the layout (inventory, examine,
 * waypoints, datapad, planet map, survey, crafting, missions, travel), the gamepad dock that stands in for several
 * of them, and the structure placement host. Registers with USWGUISubsystem; nothing here knows the holo plugin.
 */
UCLASS()
class SWGUIWINDOW_API USWGWindowUISubsystem : public ULocalPlayerSubsystem, public ISWGUIPresenter
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ISWGUIPresenter
	virtual ESWGUIPresentation GetPresentation() const override { return ESWGUIPresentation::Window; }
	virtual bool SupportsFeature(ESWGUIFeature Feature) const override;
	virtual void OpenFeature(ESWGUIFeature Feature, const FSWGUIFeatureRequest& Request) override;
	virtual void CloseFeature(ESWGUIFeature Feature) override;
	virtual bool IsFeatureOpen(ESWGUIFeature Feature) const override;
	virtual void ToggleFeature(ESWGUIFeature Feature) override;
	virtual void NotifyDataChanged(ESWGUIFeature Feature) override;
	virtual void NotifyRadialMenuClosed() override;

private:
	USWGUISubsystem* GetRouter() const;
	APlayerController* GetController() const;
	bool IsGamepadActive() const;

	/** Puts a floating window on the player screen above every other window and tracks it. */
	void ShowWindow(USWGWindowWidget* Window);
	void HandleWindowPressed(USWGWindowWidget* Window);
	void HandleWindowClosed(USWGWindowWidget* Window);
	void HandleInputMethodChanged(ECommonInputType InputType);

	void OpenInventory(bool bDocked);
	void ToggleInventory();
	void CloseInventory();
	bool IsInventoryOpen() const;
	void HandleInventoryDockClosed();
	void OpenExamine(int64 ObjectId);
	void OpenExamineFromRequest(int64 ObjectId);
	void ToggleWaypointList();
	void CloseWaypointList();
	bool IsWaypointListOpen() const;
	void ToggleDatapad();
	void CloseDatapad();
	bool IsDatapadOpen() const;
	void OpenPlanetMap();
	void OpenSurvey();
	void OpenCrafting();
	void OpenMissionBrowser(int64 TerminalObjectId);
	void HandleMissionDockClosed();
	void RefreshMissionLists();
	void OpenTravel();
	void OpenPlacement();

	UPROPERTY()
	TArray<TObjectPtr<USWGWindowWidget>> Windows;

	UPROPERTY()
	TObjectPtr<class USWGInventoryWidget> InventoryWindow;

	/** The gamepad form; only one of this and InventoryWindow is ever up. */
	UPROPERTY()
	TObjectPtr<class USWGInventoryDockWidget> InventoryDock;

	UPROPERTY()
	TObjectPtr<class USWGWaypointListWidget> WaypointWindow;

	UPROPERTY()
	TObjectPtr<class USWGDatapadWidget> DatapadWindow;

	UPROPERTY()
	TObjectPtr<class USWGPlanetMapWindowWidget> PlanetMapWindow;

	UPROPERTY()
	TObjectPtr<class USWGSurveyWidget> SurveyWindow;

	UPROPERTY()
	TObjectPtr<class USWGCraftingWidget> CraftingWindow;

	UPROPERTY()
	TObjectPtr<class USWGMissionBrowserWidget> MissionWindow;

	/** The gamepad form; only one of this and MissionWindow is ever up. */
	UPROPERTY()
	TObjectPtr<class USWGMissionBrowserDockWidget> MissionDock;

	/** Ticket purchase is one responsive window; it expands and changes controls when gamepad becomes active. */
	UPROPERTY()
	TObjectPtr<class USWGTravelWidget> TravelWindow;

	UPROPERTY()
	TMap<int64, TObjectPtr<class USWGExamineWidget>> ExamineWindows;

	UPROPERTY()
	TObjectPtr<class USWGStructurePlacementWidget> PlacementWidget;

	FDelegateHandle InputMethodChangedHandle;

	/** Each raise re-adds the window one layer higher; the layout sits at 100 and the radial at 200. */
	int32 NextWindowZ = 120;
};
