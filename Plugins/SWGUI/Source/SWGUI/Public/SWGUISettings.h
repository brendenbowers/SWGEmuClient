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

	/** Damage-number layer put under the HUD while in world. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGFloatingTextWidget> FloatingTextClass;

	/** Inventory window toggled by the player's InventoryKey. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGInventoryWidget> InventoryClass;

	/** One inventory row (WBP_InventoryRow). */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGInventoryRowWidget> InventoryRowClass;

	/** Examine window opened from the radial menu. */
	UPROPERTY(Config, EditAnywhere, Category = "Windows")
	TSoftClassPtr<class USWGExamineWidget> ExamineClass;
};
