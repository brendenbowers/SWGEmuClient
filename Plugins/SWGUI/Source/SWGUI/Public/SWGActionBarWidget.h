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
 * SWG stores the real toolbar server-side, but nothing we decode carries it
 * yet, so slots are filled locally for now.
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

	/** Re-reads the slots onto their widgets. Call after changing Slots at runtime. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|ActionBar")
	void RefreshSlotVisuals();

	/** The hotkey text for a slot — "1" through "0", then "-" and "=", as SWG numbers them. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|ActionBar")
	static FText GetSlotKeyLabel(int32 SlotIndex);

protected:
	virtual void NativeConstruct() override;

	/** Builds SlotCount slot widgets into SlotBox. */
	void BuildSlotWidgets();

	/** Puts the basic attack in slot 0 if it is empty. See bSeedDefaultAttackSlot. */
	void SeedDefaultAttackSlot();

	/** SWG's toolbar bank is twelve slots wide. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar")
	int32 SlotCount = 12;

	/** Fill leftover slots with whatever abilities the player has, on construct. */
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

	/**
	 * Slots per visual group. Retail shows 12 as three groups of four; a
	 * gamepad layout wants two groups of eight (one per shoulder button).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar", meta = (ClampMin = 1))
	int32 GroupSize = 4;

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
