#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGInventoryRowWidget.generated.h"

class UBorder;
class UModelWidget;
class UTextBlock;

DECLARE_DELEGATE_ThreeParams(FSWGOnInventoryRowPressed, int64 /*ObjectId*/, FKey /*Button*/, FVector2D /*ScreenPosition*/);

/**
 * One line of the inventory window (WBP_InventoryRow): the item's model, live and turning
 * while hovered, its name and, for gear, the slot it fills. Reports mouse presses to the window with the
 * button and screen position, so a right-click can open the radial menu
 * where the cursor is.
 */
UCLASS(Abstract)
class SWGUI_API USWGInventoryRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRow(int64 InObjectId, const FString& Name, const FString& SlotNames);

	void SetSelected(bool bSelected);

	int64 GetObjectId() const { return ObjectId; }

	FSWGOnInventoryRowPressed OnPressed;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	FLinearColor HoverColor = FLinearColor(0.33f, 0.9f, 1.f, 0.15f);

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	FLinearColor SelectedColor = FLinearColor(0.33f, 0.9f, 1.f, 0.35f);

	/** Turntable speed while the cursor is over the row, degrees per second. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	float HoverRotateSpeed = 60.f;

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	/** Tinted for hover and selection. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> Background;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UModelWidget> Model;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> SlotText;

private:
	void ApplyBackground();

	int64 ObjectId = 0;
	bool bHovered = false;
	bool bSelected = false;
};
