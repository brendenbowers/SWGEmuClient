#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SWGHoloUISettings.generated.h"

/** The hologram UI's Blueprint classes (Project Settings > Game > SWG UI (Holo)). Backed by DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SWG UI (Holo)"))
class SWGUIHOLO_API USWGHoloUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const USWGHoloUISettings& Get() { return *GetDefault<USWGHoloUISettings>(); }

	UPROPERTY(Config, EditAnywhere, Category = "Hologram")
	TSoftClassPtr<class USWGHoloMapWidget> HoloMapClass;

	UPROPERTY(Config, EditAnywhere, Category = "Hologram")
	TSoftClassPtr<class USWGHoloInventoryWidget> HoloInventoryClass;

	UPROPERTY(Config, EditAnywhere, Category = "Hologram")
	TSoftClassPtr<class USWGHoloLabelWidget> HoloLabelClass;

	UPROPERTY(Config, EditAnywhere, Category = "Hologram")
	TSoftClassPtr<class USWGHoloDetailCardWidget> HoloDetailCardClass;

	UPROPERTY(Config, EditAnywhere, Category = "Hologram")
	TSoftClassPtr<class USWGHoloAttributeLineWidget> HoloAttributeLineClass;

	UPROPERTY(Config, EditAnywhere, Category = "Hologram")
	TSoftClassPtr<class USWGHoloSurveyWidget> HoloSurveyClass;

	/** Holographic crafting overlay; unset uses its C++ controls. */
	UPROPERTY(Config, EditAnywhere, Category = "Hologram")
	TSoftClassPtr<class USWGHoloCraftingWidget> HoloCraftingClass;
};
