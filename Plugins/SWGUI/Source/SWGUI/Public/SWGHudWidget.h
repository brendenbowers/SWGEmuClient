#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "CommonInputTypeEnum.h"
#include "SWGHudWidget.generated.h"

class USWGConditionWidget;
class USWGActionBarWidget;
class USWGTargetBoxWidget;
class USWGCommandQueueWidget;
class USWGWaypointMarkerWidget;

/**
 * In-world HUD root, pushed onto the layout's HUD layer once the player is in
 * the world. Holds the condition readout and the action bar; the Blueprint
 * lays them out and binds them by name.
 */
UCLASS(Abstract)
class SWGUI_API USWGHudWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Opens the inventory window, or closes it if it is up. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HUD")
	void ToggleInventory();

	/** Opens the waypoint list window, or closes it if it is up. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HUD")
	void ToggleWaypointList();

	/** Fires the action bar slot at this index — the entry point for number-key hotkeys. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HUD")
	bool TriggerActionSlot(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|HUD")
	USWGActionBarWidget* GetActionBar() const { return ActionBar; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|HUD")
	USWGTargetBoxWidget* GetTargetBox() const { return TargetBox; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|HUD")
	USWGCommandQueueWidget* GetCommandQueue() const { return CommandQueue; }

	/** The HUD showing for the current session, or null if none is up. */
	static USWGHudWidget* GetActiveHud();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USWGConditionWidget> ConditionPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USWGActionBarWidget> ActionBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USWGTargetBoxWidget> TargetBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USWGCommandQueueWidget> CommandQueue;

	/** Pins active waypoints over the 3D world and draws an off-screen arrow for the rest — see USWGWaypointMarkerWidget. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USWGWaypointMarkerWidget> WaypointMarkers;

private:
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	void BindHotkeys(APawn* Pawn);
	void HandleActionSlotHotkey(int32 SlotIndex);
	void HandleActionBankChanged(int32 BankIndex);

	/** Swaps the action bar between its keyboard and gamepad layouts as the last-used device changes. */
	void HandleInputMethodChanged(ECommonInputType InputType);

	/** Damage numbers. Lives on the player screen under the layout, not in this tree, so the Blueprint needn't know about it. */
	UPROPERTY()
	TObjectPtr<class USWGFloatingTextWidget> FloatingText;

	TWeakObjectPtr<class ASWGPlayer> HotkeySource;
	FDelegateHandle InputMethodChangedHandle;

	static TWeakObjectPtr<USWGHudWidget> ActiveHud;
};
