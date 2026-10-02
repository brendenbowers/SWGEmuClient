#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SWGUITypes.h"
#include "SWGUICommonSettings.generated.h"

/** Where each feature opens by default when both presentations are available (Project Settings > Game > SWG UI (Common)). Backed by DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SWG UI (Common)"))
class SWGUICOMMON_API USWGUICommonSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const USWGUICommonSettings& Get() { return *GetDefault<USWGUICommonSettings>(); }

	/** The swg.UI.Mode console command changes these live. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	ESWGUIPresentation InventoryPresentation = ESWGUIPresentation::Window;

	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	ESWGUIPresentation PlanetMapPresentation = ESWGUIPresentation::Holo;

	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	ESWGUIPresentation SurveyPresentation = ESWGUIPresentation::Holo;

	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	ESWGUIPresentation CraftingPresentation = ESWGUIPresentation::Holo;
};
