#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "SWGUITypes.h"
#include "SWGHoloUISubsystem.generated.h"

class USWGUISubsystem;

/**
 * The hologram presentation of the UI features that have one: inventory, planet map, survey and crafting. Every
 * hologram takes over the camera, so only one is up at a time; opening one steps the others aside. Registers with
 * USWGUISubsystem; nothing here knows the window plugin.
 */
UCLASS()
class SWGUIHOLO_API USWGHoloUISubsystem : public ULocalPlayerSubsystem, public ISWGUIPresenter
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ISWGUIPresenter
	virtual ESWGUIPresentation GetPresentation() const override { return ESWGUIPresentation::Holo; }
	virtual bool SupportsFeature(ESWGUIFeature Feature) const override;
	virtual bool CanOpenFeature(ESWGUIFeature Feature) const override;
	virtual void OpenFeature(ESWGUIFeature Feature, const FSWGUIFeatureRequest& Request) override;
	virtual void CloseFeature(ESWGUIFeature Feature) override;
	virtual bool IsFeatureOpen(ESWGUIFeature Feature) const override;

private:
	USWGUISubsystem* GetRouter() const;
	APlayerController* GetController() const;

	/** Holo crafting is the one hologram that yields to the others by becoming the crafting window instead of closing. */
	void StepAsideCrafting();

	void OpenInventory();
	void OpenPlanetMap();
	void OpenSurvey();
	void OpenCrafting();
	void HandleHoloInventoryClosed();
	void HandleHoloMapClosed();

	UPROPERTY()
	TObjectPtr<class USWGHoloInventoryWidget> HoloInventory;

	UPROPERTY()
	TObjectPtr<class USWGHoloMapWidget> HoloMap;

	UPROPERTY()
	TObjectPtr<class USWGHoloSurveyWidget> HoloSurvey;

	UPROPERTY()
	TObjectPtr<class USWGHoloCraftingWidget> HoloCrafting;
};
