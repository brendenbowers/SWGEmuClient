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
};
