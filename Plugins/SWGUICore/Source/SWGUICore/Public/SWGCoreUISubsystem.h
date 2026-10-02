#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Flow/SWGClientState.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "Subsystems/SWGSuiSubsystem.h"
#include "SWGUITypes.h"
#include "SWGCoreUISubsystem.generated.h"

class USWGGameLayout;
class USWGRadialMenuWidget;
class USWGSuiBoxWidget;
class USWGUISubsystem;

/**
 * The Core presentation: what every client shows whichever window or holo style is in use. Owns the layout shell and
 * the client-state screens (login to zone loading), the in-world HUD, the radial menu and SUI boxes, and registers
 * with the router as the presenter of the Hud feature. Reacts to the flow, radial menu and SUI gameplay events itself
 * since their widgets are its own.
 */
UCLASS()
class SWGUICORE_API USWGCoreUISubsystem : public ULocalPlayerSubsystem, public ISWGUIPresenter
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

	// ISWGUIPresenter
	virtual ESWGUIPresentation GetPresentation() const override { return ESWGUIPresentation::Core; }
	virtual bool SupportsFeature(ESWGUIFeature Feature) const override { return Feature == ESWGUIFeature::Hud; }
	virtual void OpenFeature(ESWGUIFeature Feature, const FSWGUIFeatureRequest& Request) override;
	virtual void CloseFeature(ESWGUIFeature Feature) override;
	virtual bool IsFeatureOpen(ESWGUIFeature Feature) const override;
	virtual float GetHudOpacity() const override;
	virtual void SetHudOpacity(float Opacity) override;

	/** The layout shell, created on demand. Null when no layout class is configured. */
	USWGGameLayout* EnsureLayout();

private:
	UFUNCTION()
	void HandleStateChanged(ESWGClientState OldState, ESWGClientState NewState);

	UFUNCTION()
	void HandleRadialMenuReceived(const FSWGRadialMenu& Menu);

	UFUNCTION()
	void HandleSuiPageOpened(const FSWGSuiPage& Page);

	UFUNCTION()
	void HandleSuiPageClosed(int32 PageId);

	void HandleRadialMenuClosed();

	USWGUISubsystem* GetRouter() const;

	UPROPERTY()
	TObjectPtr<USWGRadialMenuWidget> RadialMenu;

	/** Open SUI windows by page id, so a server force-close can take them down. */
	UPROPERTY()
	TMap<int32, TObjectPtr<USWGSuiBoxWidget>> SuiWindows;
};
