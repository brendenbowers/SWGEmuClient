#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "SWGHudWidget.generated.h"

class USWGConditionWidget;
class USWGActionBarWidget;

/**
 * In-world HUD root, pushed onto the layout's HUD layer once the player is in
 * the world. Holds the condition readout and the action bar; the Blueprint
 * lays them out and binds them by name.
 */
UCLASS(Abstract)
class SWGEMUCLIENT_API USWGHudWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Fires the action bar slot at this index — the entry point for number-key hotkeys. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HUD")
	bool TriggerActionSlot(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|HUD")
	USWGActionBarWidget* GetActionBar() const { return ActionBar; }

	/** The HUD showing for the current session, or null if none is up. */
	static USWGHudWidget* GetActiveHud();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USWGConditionWidget> ConditionPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USWGActionBarWidget> ActionBar;

private:
	static TWeakObjectPtr<USWGHudWidget> ActiveHud;
};
