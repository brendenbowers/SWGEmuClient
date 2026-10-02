#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SWGCoreUISettings.generated.h"

class USWGGameLayout;
class UDataTable;
class UCommonActivatableWidget;

/** Wiring for the core presentation (Project Settings > Game > SWG UI (Core)). Backed by DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SWG UI (Core)"))
class SWGUICORE_API USWGCoreUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const USWGCoreUISettings& Get() { return *GetDefault<USWGCoreUISettings>(); }

	/** Root layout widget (WBP_ClientShell) created for each local player. */
	UPROPERTY(Config, EditAnywhere, Category = "Layout")
	TSoftClassPtr<USWGGameLayout> LayoutClass;

	/** FSWGStateTransitionRow table: which widget to push on which client-state change. */
	UPROPERTY(Config, EditAnywhere, Category = "Layout")
	TSoftObjectPtr<UDataTable> StateTransitionTable;

	/** The in-world HUD (WBP_Hud), for opening the Hud feature outside the state-transition table. */
	UPROPERTY(Config, EditAnywhere, Category = "Widgets")
	TSoftClassPtr<UCommonActivatableWidget> HudClass;

	/** Object context menu opened by right-clicking an object (WBP_RadialMenu). */
	UPROPERTY(Config, EditAnywhere, Category = "Widgets")
	TSoftClassPtr<class USWGRadialMenuWidget> RadialMenuClass;

	/** Server-driven SUI box (WBP_SuiBox). */
	UPROPERTY(Config, EditAnywhere, Category = "Widgets")
	TSoftClassPtr<class USWGSuiBoxWidget> SuiBoxClass;

	/** Floating combat/status text over the world (WBP_FloatingText). */
	UPROPERTY(Config, EditAnywhere, Category = "Widgets")
	TSoftClassPtr<class USWGFloatingTextWidget> FloatingTextClass;
};
