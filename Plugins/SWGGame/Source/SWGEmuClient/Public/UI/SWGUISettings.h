#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SWGUISettings.generated.h"

class USWGGameLayout;
class UDataTable;

/** Project-wide UI wiring (Project Settings > Game > SWG UI). Backed by DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SWG UI"))
class SWGEMUCLIENT_API USWGUISettings : public UDeveloperSettings
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
};
