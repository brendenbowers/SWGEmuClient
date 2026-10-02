#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "SWGUITypes.h"
#include "SWGUISubsystem.generated.h"

/**
 * Routes every UI feature to a presentation. It draws nothing itself.
 *
 * Gameplay events (examine, missions, travel, survey, crafting, placement) become features, opened through
 * OpenFeature/ToggleFeature, which hands each to the presenter for the feature's current presentation. The UI
 * plugins register a presenter here (SWGUICore the HUD and shell, SWGUIWindow the windows, SWGUIHolo the holograms);
 * this class never names a widget of any. Switching a feature's presentation closes it in one and reopens it in the
 * other.
 */
UCLASS()
class SWGUICOMMON_API USWGUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Called by a visual plugin's presenter subsystem as it starts and stops. */
	void RegisterPresenter(ISWGUIPresenter& Presenter);
	void UnregisterPresenter(ISWGUIPresenter& Presenter);
	ISWGUIPresenter* GetPresenter(ESWGUIPresentation Presentation) const { return Presenters.FindRef(Presentation); }

	/** A presenter's view of a feature opened or closed on its own (the player closed a window). */
	void NotifyFeatureStateChanged(ESWGUIFeature Feature);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void OpenFeature(ESWGUIFeature Feature, const FSWGUIFeatureRequest& Request);

	/** Opens the feature in its current presentation, or closes it if it is up. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void ToggleFeature(ESWGUIFeature Feature);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void CloseFeature(ESWGUIFeature Feature);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|UI")
	bool IsFeatureOpen(ESWGUIFeature Feature) const;

	/** Switches where a feature opens; an open one is swapped for the other presentation in place. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void SetPresentation(ESWGUIFeature Feature, ESWGUIPresentation Presentation);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|UI")
	ESWGUIPresentation GetPresentation(ESWGUIFeature Feature) const;

	/** True while the active device is a gamepad. */
	bool IsGamepadActive() const;

	/** The radial menu closed (the Core presenter owns it); every presenter hears of it, a gamepad dock takes focus back. */
	void NotifyRadialMenuClosed();

	/** The HUD's opacity, held by whichever presenter owns the HUD; 1 when none does. */
	float GetHudOpacity() const;
	void SetHudOpacity(float Opacity);

	// Blueprint-friendly shortcuts for the features the HUD binds to keys.
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void ToggleInventory() { ToggleFeature(ESWGUIFeature::Inventory); }

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void CloseInventory() { CloseFeature(ESWGUIFeature::Inventory); }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|UI")
	bool IsInventoryOpen() const { return IsFeatureOpen(ESWGUIFeature::Inventory); }

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void ToggleWaypointList() { ToggleFeature(ESWGUIFeature::WaypointList); }

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void TogglePlanetMap() { ToggleFeature(ESWGUIFeature::PlanetMap); }

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void ToggleDatapad() { ToggleFeature(ESWGUIFeature::Datapad); }

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void OpenExamine(int64 ObjectId);

private:
	UFUNCTION()
	void HandleExamineRequested(int64 ObjectId);

	UFUNCTION()
	void HandleMissionWindowRequested(int64 TerminalObjectId);

	UFUNCTION()
	void HandleMissionListChanged();

	UFUNCTION()
	void HandleTravelWindowRequested();

	UFUNCTION()
	void HandleSurveyWindowRequested();

	UFUNCTION()
	void HandleCraftingSessionStarted();

	UFUNCTION()
	void HandlePlacementStarted();

	/** The presenter that has the feature open, or else the one for its current presentation. */
	ISWGUIPresenter* FindOpenPresenter(ESWGUIFeature Feature) const;
	ISWGUIPresenter* ResolvePresenter(ESWGUIFeature Feature) const;

	/** Tells the survey subsystem whether any survey form is up, so maps know to draw the scan. */
	void RefreshSurveyToolActive();

	TMap<ESWGUIPresentation, ISWGUIPresenter*> Presenters;
	TMap<ESWGUIFeature, ESWGUIPresentation> Presentations;
};
