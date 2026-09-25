#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGHoloLabelWidget.generated.h"

class UBorder;
class UTextBlock;

DECLARE_DELEGATE_TwoParams(FSWGOnHoloLabelHovered, int64 /*ObjectId*/, bool /*bHovered*/);
DECLARE_DELEGATE_ThreeParams(FSWGOnHoloLabelPressed, int64 /*ObjectId*/, FKey /*Button*/, FVector2D /*ScreenPosition*/);

/**
 * One item's name floating beside the holo figure or in the bag list: a
 * translucent panel with a glowing edge. Reports hover and presses with its
 * item, so the owner can show details or open the item's radial menu.
 *
 * WBP_HoloLabel lays it out, binding Panel and Label; without one it builds
 * its own. Either way it restyles the panel for hover unless bApplyHoloStyle
 * is off, when OnBrightChanged is the Blueprint's cue to do it itself.
 */
UCLASS(Blueprintable)
class SWGUI_API USWGHoloLabelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** A label of the class set in Project Settings > SWG UI (HoloLabelClass), or this one. */
	static USWGHoloLabelWidget* Create(APlayerController* Owner);

	void SetItem(int64 InObjectId, const FText& Text);
	int64 GetObjectId() const { return ObjectId; }

	/** Wraps the name onto more lines past this width, slate units; zero never wraps. */
	void SetWrapWidth(float Width);

	/** Lit as if hovered, e.g. while its details are pinned open. */
	void SetLit(bool bInLit);
	bool IsHovered() const { return bHovered; }

	FSWGOnHoloLabelHovered OnHovered;
	FSWGOnHoloLabelPressed OnPressed;

	/** Hovered or lit changed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SWGEmu|Hologram")
	void OnBrightChanged(bool bBright);

	/** Off to leave the panel's look to the Blueprint (OnBrightChanged). */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Hologram")
	bool bApplyHoloStyle = true;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Panel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Label;

private:
	void ApplyStyle();

	int64 ObjectId = 0;
	bool bHovered = false;
	bool bLit = false;
};
