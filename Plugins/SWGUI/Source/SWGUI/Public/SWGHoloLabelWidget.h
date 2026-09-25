#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGHoloLabelWidget.generated.h"

class UBorder;
class UTextBlock;

DECLARE_DELEGATE_TwoParams(FSWGOnHoloLabelHovered, int64 /*ObjectId*/, bool /*bHovered*/);
DECLARE_DELEGATE_ThreeParams(FSWGOnHoloLabelPressed, int64 /*ObjectId*/, FKey /*Button*/, FVector2D /*ScreenPosition*/);

/**
 * One item's name floating beside the holo figure or shelf: a translucent
 * panel with a glowing edge. Reports hover and presses with its item, so the
 * owner can show details or open the item's radial menu.
 */
UCLASS()
class SWGUI_API USWGHoloLabelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetItem(int64 InObjectId, const FText& Text);

	/** Wraps the name onto more lines past this width, slate units; zero never wraps. */
	void SetWrapWidth(float Width);
	int64 GetObjectId() const { return ObjectId; }

	/** Lit as if hovered, e.g. while its details are pinned open. */
	void SetLit(bool bInLit);
	bool IsHovered() const { return bHovered; }

	FSWGOnHoloLabelHovered OnHovered;
	FSWGOnHoloLabelPressed OnPressed;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void ApplyStyle();

	UPROPERTY()
	TObjectPtr<UBorder> Panel;

	UPROPERTY()
	TObjectPtr<UTextBlock> Label;

	int64 ObjectId = 0;
	bool bHovered = false;
	bool bLit = false;
};
