#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Flow/SWGClientState.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "Subsystems/SWGSuiSubsystem.h"
#include "SWGUISubsystem.generated.h"

class USWGGameLayout;

/**
 * Owns the UI side of the client flow: creates the layout for the local
 * player and pushes/clears layer widgets as USWGClientFlowSubsystem changes
 * state, driven by USWGUISettings. The flow subsystem itself knows nothing
 * about widgets.
 */
UCLASS()
class SWGUI_API USWGUISubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

private:
	UFUNCTION()
	void HandleStateChanged(ESWGClientState OldState, ESWGClientState NewState);

	UFUNCTION()
	void HandleRadialMenuReceived(const FSWGRadialMenu& Menu);

	UFUNCTION()
	void HandleSuiPageOpened(const FSWGSuiPage& Page);

	UFUNCTION()
	void HandleSuiPageClosed(int32 PageId);

	USWGGameLayout* EnsureLayout();

	UPROPERTY()
	TObjectPtr<class USWGRadialMenuWidget> RadialMenu;

	/** Open SUI windows by page id, so a server force-close can take them down. */
	UPROPERTY()
	TMap<int32, TObjectPtr<class USWGSuiBoxWidget>> SuiWindows;
};
