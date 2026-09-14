#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "SWGActionSlotWidget.generated.h"

class USWGActionBarWidget;

/**
 * A single toolbar slot: its hotkey number and whatever command sits in it.
 * The bar creates these, one per slot, and tells each one its index.
 */
UCLASS(Abstract)
class SWGEMUCLIENT_API USWGActionSlotWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Points the slot at its bar and index, and sets what it shows. */
	void InitialiseSlot(USWGActionBarWidget* InOwningBar, int32 InSlotIndex, const FText& InKeyLabel);

	/** Updates the command shown. Empty text for an unassigned slot. */
	void SetCommandLabel(const FText& InLabel);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|ActionBar")
	int32 GetSlotIndex() const { return SlotIndex; }

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SlotButton;

	/** The hotkey this slot answers to — "1" through "=". */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CommandLabel;

	UFUNCTION()
	void HandleClicked();

private:
	UPROPERTY()
	TWeakObjectPtr<USWGActionBarWidget> OwningBar;

	int32 SlotIndex = INDEX_NONE;

	/** Held until NativeConstruct, since the bar initialises slots before they construct. */
	FText PendingKeyLabel;
	FText PendingCommandLabel;
};
