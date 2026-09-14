#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "SWGActionSlotWidget.generated.h"

class USWGActionBarWidget;

/**
 * A single toolbar slot: its hotkey number and whatever command sits in it.
 * The bar creates these, one per slot, and tells each one its index.
 */
UCLASS(Abstract)
class SWGUI_API USWGActionSlotWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Points the slot at its bar and index, and sets what it shows. */
	void InitialiseSlot(USWGActionBarWidget* InOwningBar, int32 InSlotIndex, const FText& InKeyLabel);

	/** Updates the command shown. Empty text for an unassigned slot. */
	void SetCommandLabel(const FText& InLabel);

	/** Shows Brush as the slot icon, or hides the icon image for a null brush. */
	void SetCommandIcon(const FSlateBrush* Brush);

	/** The cell background behind the icon; null leaves whatever the Blueprint painted. */
	void SetFrame(const FSlateBrush* Brush);

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

	/** Retail toolbar icon for the command, behind the labels. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> CommandIcon;

	/** Cell background behind the icon. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> SlotFrame;

	/** Retail's toolbar glyph colour (buttonBar.all NormalIconColor). The sheet's glyphs are white. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar")
	FLinearColor IconTint = FLinearColor::FromSRGBColor(FColor(0x54, 0xE4, 0xFE));

	/** Retail's neutral cell colour (icon.neutral.rs_default). */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|ActionBar")
	FLinearColor FrameTint = FLinearColor::FromSRGBColor(FColor(0x00, 0xD6, 0xFB));

	UFUNCTION()
	void HandleClicked();

private:
	UPROPERTY()
	TWeakObjectPtr<USWGActionBarWidget> OwningBar;

	int32 SlotIndex = INDEX_NONE;

	/** Held until NativeConstruct, since the bar initialises slots before they construct. */
	FText PendingKeyLabel;
	FText PendingCommandLabel;
	FSlateBrush PendingIconBrush;
	bool bHasPendingIcon = false;
	FSlateBrush PendingFrameBrush;
	bool bHasPendingFrame = false;

	void ApplyIcon();
	void ApplyFrame();
};
