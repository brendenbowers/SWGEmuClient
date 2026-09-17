#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "Components/PanelWidget.h"
#include "SWGActionSlotWidget.h"

#include "SWGActionBarWidget.generated.h"

/**
 * One toolbar slot. CommandName is the server command ("burstrun", "attack") —
 * the same name Core3 registers, hashed on send.
 *
 * SWG keeps the toolbar client-side, in the retail profile's .uis file; see
 * LoadRetailToolbar.
 */
USTRUCT(BlueprintType)
struct SWGUI_API FSWGActionSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|ActionBar")
	FString CommandName;

	/** Overrides the retail name (cmd_n.stf) on the slot. Leave empty to use the resolved name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|ActionBar")
	FText Label;

	/** Sent along with the command — "sad" for a retail "/mood sad" slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|ActionBar")
	FString Arguments;

	bool IsEmpty() const { return CommandName.IsEmpty(); }
};

/**
 * The player's action bar. Holds the slots and turns a press into a queued
 * command; the Blueprint owns how a slot looks.
 */
UCLASS(Abstract)
class SWGUI_API USWGActionBarWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Slot contents, index 0 first. Editable per-Blueprint until the real toolbar arrives. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SWGEmu|ActionBar")
	TArray<FSWGActionSlot> Slots;

	/** Sends the slot's command, targeting whatever the player currently has selected. Returns false if the slot is empty or the send failed. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|ActionBar")
	bool TriggerSlot(int32 SlotIndex);

	/** Sends a command directly, bypassing the slots — the path a chat "/command" would use. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|ActionBar")
	bool SendCommand(const FString& CommandName, const FString& Arguments);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|ActionBar")
	void SetSlotCommand(int32 SlotIndex, const FString& CommandName, FText Label);

	/** Fired after a slot is sent, for press feedback. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SWGEmu|ActionBar")
	void OnSlotTriggered(int32 SlotIndex);

	/** The abilities the player actually has, from the player object's base9 — what a slot picker would list. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|ActionBar")
	TArray<FString> GetAvailableAbilities() const;

	/**
	 * Drops the player's abilities into any slot that's still empty, skipping
	 * ones already placed. A stand-in until the real server-side toolbar is
	 * decoded — that's what decides slot contents in SWG.
	 * Returns how many slots were filled.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|ActionBar")
	int32 FillEmptySlotsFromAbilities();

	/**
	 * Fills the slots from the retail client's toolbar for this character —
	 * pane 0 of profiles/<account>/<galaxy>/<oid>.uis under the TRE directory,
	 * so the bar matches what the player set up in the original client. Item
	 * slots are left empty. Returns false if the file is missing or has no
	 * toolbar.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|ActionBar")
	bool LoadRetailToolbar();

	/** Re-reads the slots onto their widgets. Call after changing Slots at runtime. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|ActionBar")
	void RefreshSlotVisuals();

	/** The hotkey text for a slot — "1" through "0", then "-" and "=", as SWG numbers them. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|ActionBar")
	static FText GetSlotKeyLabel(int32 SlotIndex);

	/** The gamepad button for a slot: D-pad arrows for 0-3, A B X Y for 4-7, repeating for the second bank. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|ActionBar")
	static FText GetGamepadSlotKeyLabel(int32 SlotIndex);

	/**
	 * Rebuilds as two banks of eight (D-pad + face buttons, shifted by a
	 * trigger) or back to the keyboard's twelve. The HUD calls this as the
	 * last-used input device changes.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|ActionBar")
	void SetGamepadLayout(bool bGamepad);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|ActionBar")
	bool IsGamepadLayout() const { return bGamepadLayout; }

	/** Highlights the bank the gamepad's buttons currently fire into. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|ActionBar")
	void SetActiveBank(int32 BankIndex);

protected:
	virtual void NativeConstruct() override;

	/** Builds the slot widgets for the current layout into SlotBox. */
	void BuildSlotWidgets();

	/** Slots the current layout shows: SlotCount, or both gamepad banks. */
	int32 GetActiveSlotCount() const { return bGamepadLayout ? GamepadBankCount * GamepadBankSize : SlotCount; }

	/** Builds one slot widget, wired to SlotIndex, or null if the class fails to instantiate. */
	USWGActionSlotWidget* MakeSlotWidget(int32 SlotIndex, const FSlateBrush* FrameBrush);

	/** Keyboard: groups of GroupSize in a row. */
	void BuildKeyboardSlots(const FSlateBrush* FrameBrush);

	/** Gamepad: per bank, a D-pad diamond and a face-button diamond. */
	void BuildGamepadSlots(const FSlateBrush* FrameBrush);

	/** Dims every group but the active bank in the gamepad layout; all groups full opacity otherwise. */
	void ApplyBankHighlight();

	/** Puts the basic attack in slot 0 if it is empty. See bSeedDefaultAttackSlot. */
	void SeedDefaultAttackSlot();

	/** SWG's toolbar bank is twelve slots wide. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar")
	int32 SlotCount = 12;

	/** Take the slots from the retail client's saved toolbar on construct, when it has one. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar")
	bool bLoadRetailToolbar = true;

	/** Fill leftover slots with whatever abilities the player has, on construct. Skipped when the retail toolbar loaded. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar")
	bool bFillEmptySlotsFromAbilities = true;

	/**
	 * Put the basic attack in the first slot on construct, if that slot is
	 * empty. It never arrives through GetAvailableAbilities — "attack" is not
	 * a character ability, it is the one combat command the server lets every
	 * player use unconditionally (ObjectController::activateCommand exempts it
	 * by name from its command-group check) — so without this there is no way
	 * to reach it from the bar at all.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar")
	bool bSeedDefaultAttackSlot = true;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar")
	TSubclassOf<USWGActionSlotWidget> SlotWidgetClass;

	/** Slots per visual group. Retail shows 12 as three groups of four. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar", meta = (ClampMin = 1))
	int32 GroupSize = 4;

	/**
	 * Gamepad layout: two banks the trigger toggles between, each drawn as
	 * two diamonds — D-pad on the left, face buttons on the right — so a
	 * slot sits where its button is on the pad.
	 */
	static constexpr int32 GamepadBankCount = 2;
	static constexpr int32 GamepadBankSize = 8;

	/** Gap between a bank's two diamonds, in slate units. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar|Gamepad")
	float DiamondSpacing = 4.f;

	/** Gap between the two banks, in slate units. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar|Gamepad")
	float BankSpacing = 24.f;

	/** Each bank is scaled by this; the tiles are squat (one-line names), so full size fits. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar|Gamepad", meta = (ClampMin = 0.25, ClampMax = 1))
	float GamepadSlotScale = 1.f;

	/**
	 * How far each arm of a diamond is pulled toward its empty centre, in
	 * slate units. The arms only ever meet the centre cell, so they can
	 * overlap it without covering each other.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar|Gamepad", meta = (ClampMin = 0))
	float DiamondOverlap = 14.f;

	/** Breathing room around every tile in a diamond, in slate units, so neighbouring arms don't touch corner to corner. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar|Gamepad", meta = (ClampMin = 0))
	float TileSpacing = 3.f;

	/** Render opacity of the bank the trigger is not currently selecting. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar|Gamepad", meta = (ClampMin = 0, ClampMax = 1))
	float InactiveBankOpacity = 0.4f;

	/** Gap between groups, in slate units. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar")
	float GroupSpacing = 16.f;

	/** Retail style drawn behind every slot (see ui_styles.inc): the toolbar's neutral cell background. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar")
	FString SlotFrameStyle = TEXT("icon.neutral.default.c");

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> SlotBox;

	UPROPERTY()
	TArray<TObjectPtr<USWGActionSlotWidget>> SlotWidgets;

	/** One per visual group, in slot order — the gamepad banks. */
	UPROPERTY()
	TArray<TObjectPtr<UPanelWidget>> GroupWidgets;

	bool bGamepadLayout = false;
	int32 ActiveBank = 0;

	/** The slots came from the retail .uis, so a relayout re-reads it rather than ability-filling. */
	bool bRetailToolbarLoaded = false;

private:
	/** The player's current target, or 0 when nothing is targeted. */
	int64 ResolveTargetId() const;

	/** Retail display name from string/<lang>/cmd_n.stf, falling back to the raw command. */
	FText ResolveCommandName(const FString& CommandName) const;

	/** Brush over the retail icon sheet for the command, or nullptr when the sheet has no icon for it. */
	const FSlateBrush* ResolveCommandIcon(const FString& CommandName);

	/** Brush for any ui_styles.inc image style ("icon.neutral.default.c"), or nullptr if it or its sheet is missing. */
	const FSlateBrush* ResolveStyleBrush(const FString& DottedPath);

	/** Style path / command name (lowercase) -> brush, so slots refreshing repeatedly don't rebuild brushes. */
	TMap<FString, FSlateBrush> StyleBrushes;
};
