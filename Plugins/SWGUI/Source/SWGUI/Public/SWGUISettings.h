#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SWGUISettings.generated.h"

class USWGGameLayout;
class UDataTable;

/** Project-wide UI wiring (Project Settings > Game > SWG UI). Backed by DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SWG UI"))
class SWGUI_API USWGUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const USWGUISettings& Get() { return *GetDefault<USWGUISettings>(); }

	/** Root layout widget (WBP_ClientShell) created for each local player. */
	UPROPERTY(Config, EditAnywhere, Category = "Layout")
	TSoftClassPtr<USWGGameLayout> LayoutClass;

	/** FSWGStateTransitionRow table: which widget to push on which client-state change. */
	UPROPERTY(Config, EditAnywhere, Category = "Layout")
	TSoftObjectPtr<UDataTable> StateTransitionTable;

	/** Object context menu opened by right-clicking an object (WBP_RadialMenu). */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGRadialMenuWidget> RadialMenuClass;

	/** Server UI window for message/list/input boxes (WBP_SuiBox), pushed on the modal layer. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGSuiBoxWidget> SuiBoxClass;

	/** Mission terminal browser (WBP_MissionBrowser), pushed on the modal layer. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGMissionBrowserWidget> MissionBrowserClass;

	/** Gamepad form of the mission browser (WBP_MissionBrowserDock), used instead of MissionBrowserClass while a gamepad is the active input. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGMissionBrowserDockWidget> MissionBrowserDockClass;

	/** Damage-number layer put under the HUD while in world. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGFloatingTextWidget> FloatingTextClass;

	/** Inventory window toggled by the player's InventoryKey. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGInventoryWidget> InventoryClass;

	/** Gamepad form of the inventory: a panel docked to the screen edge (WBP_InventoryDock), used instead of InventoryClass while a gamepad is the active input. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGInventoryDockWidget> InventoryDockClass;

	/** One inventory row (WBP_InventoryRow), shared by the window and the dock. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGInventoryRowWidget> InventoryRowClass;

	/** One attribute line or category header (WBP_ExamineLine), used by the examine window and the dock's details. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGExamineLineWidget> ExamineLineClass;

	/** Examine window opened from the radial menu. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGExamineWidget> ExamineClass;

	/** Datapad waypoint list, toggled by the player's WaypointListKey. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGWaypointListWidget> WaypointListClass;
};
