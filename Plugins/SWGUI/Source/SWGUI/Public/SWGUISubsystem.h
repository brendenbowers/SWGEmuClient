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

	/** Opens the inventory window, or closes it if it is up. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void ToggleInventory();

	/** Opens an examine window for the object, or raises the one already showing it. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void OpenExamine(int64 ObjectId);

private:
	UFUNCTION()
	void HandleExamineRequested(int64 ObjectId);

	/** Puts a floating window on the player screen above every other window and tracks it. */
	void ShowWindow(class USWGWindowWidget* Window);
	void HandleWindowPressed(class USWGWindowWidget* Window);
	void HandleWindowClosed(class USWGWindowWidget* Window);

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

	/** Floating windows (inventory, examine...) on the player screen, above the layout. */
	UPROPERTY()
	TArray<TObjectPtr<class USWGWindowWidget>> Windows;

	UPROPERTY()
	TObjectPtr<class USWGInventoryWidget> InventoryWindow;

	UPROPERTY()
	TMap<int64, TObjectPtr<class USWGExamineWidget>> ExamineWindows;

	/** Each raise re-adds the window one layer higher; the layout sits at 100 and the radial at 200. */
	int32 NextWindowZ = 120;
};
