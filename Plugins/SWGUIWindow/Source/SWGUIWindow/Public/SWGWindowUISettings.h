#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SWGWindowUISettings.generated.h"

/** The window UI's Blueprint classes (Project Settings > Game > SWG UI (Window)). Backed by DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SWG UI (Window)"))
class SWGUIWINDOW_API USWGWindowUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const USWGWindowUISettings& Get() { return *GetDefault<USWGWindowUISettings>(); }

	/** Mission terminal browser window (WBP_MissionBrowser) and its gamepad dock (WBP_MissionBrowserDock). */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGMissionBrowserWidget> MissionBrowserClass;

	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGMissionBrowserDockWidget> MissionBrowserDockClass;

	/** Ticket purchase window (WBP_Travel). */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGTravelWidget> TravelClass;

	/** Inventory window (WBP_Inventory), its gamepad dock, and the row both build per item. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGInventoryWidget> InventoryClass;

	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGInventoryDockWidget> InventoryDockClass;

	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGInventoryRowWidget> InventoryRowClass;

	/** Examine window (WBP_Examine) and the line widget it builds per attribute. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGExamineWidget> ExamineClass;

	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGExamineLineWidget> ExamineLineClass;

	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGWaypointListWidget> WaypointListClass;

	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGPlanetMapWindowWidget> PlanetMapClass;

	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGSurveyWidget> SurveyClass;

	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGDatapadWidget> DatapadClass;

	/** Crafting tool window (WBP_Crafting), opened when a crafting session starts. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGCraftingWidget> CraftingClass;
};
