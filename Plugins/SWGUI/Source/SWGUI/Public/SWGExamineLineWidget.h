#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGExamineLineWidget.generated.h"

class UTextBlock;

/**
 * One line of an item's attributes (WBP_ExamineLine): "Label: Value", or a
 * category header above a group of them. Used by the examine window and the
 * gamepad dock's details column.
 */
UCLASS(Abstract)
class SWGUI_API USWGExamineLineWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** An attribute line; indented when it sits under a category header. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Examine")
	void SetLine(const FText& Label, const FText& Value, bool bIndented);

	/** A category header: the label alone, no value, no indent. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Examine")
	void SetHeader(const FText& Category);

	/** Space above and below a header in the list; lines sit flush. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Examine")
	FMargin HeaderPadding = FMargin(0.f, 8.f, 0.f, 2.f);

	/** Colour swap for headers — retail prints them plain white above the tinted lines. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Examine")
	FLinearColor HeaderColor = FLinearColor::White;

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> ValueText;

	/** Collapsed on headers and top-level lines; any widget with a width works. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UWidget> Indent;

private:
	/** The label's colour as designed, so a header's white can be undone if the row is reused. */
	FSlateColor LineLabelColor;
	bool bLabelColorCaptured = false;

	void CaptureLabelColor();
};
