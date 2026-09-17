#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGInventoryRowWidget.generated.h"

class UBorder;
class UModelWidget;
class UTextBlock;

DECLARE_DELEGATE_ThreeParams(FSWGOnInventoryRowPressed, int64 /*ObjectId*/, FKey /*Button*/, FVector2D /*ScreenPosition*/);

/**
 * One line of the inventory window: the item's model, live and turning
 * while hovered, its name and, for gear, the slot it fills. Reports mouse presses to the window with the
 * button and screen position, so a right-click can open the radial menu
 * where the cursor is.
 */
UCLASS()
class SWGUI_API USWGInventoryRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRow(int64 InObjectId, const FString& Name, const FString& SlotNames);

	void SetSelected(bool bSelected);

	int64 GetObjectId() const { return ObjectId; }

	FSWGOnInventoryRowPressed OnPressed;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	FLinearColor NameColor = FLinearColor::FromSRGBColor(FColor(0x54, 0xE4, 0xFE));

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	FLinearColor SlotColor = FLinearColor(0.6f, 0.6f, 0.6f);

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	FLinearColor HoverColor = FLinearColor(0.33f, 0.9f, 1.f, 0.15f);

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	FLinearColor SelectedColor = FLinearColor(0.33f, 0.9f, 1.f, 0.35f);

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	int32 FontSize = 14;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	float IconSize = 56.f;

	/** Turntable speed while the cursor is over the row, degrees per second. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	float HoverRotateSpeed = 60.f;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

private:
	void ApplyBackground();

	/** Pushes Name / SlotNames / ObjectId onto the text and model widgets. */
	void ApplyRow();

	UPROPERTY()
	TObjectPtr<UBorder> Background;

	UPROPERTY()
	TObjectPtr<UModelWidget> Model;

	UPROPERTY()
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY()
	TObjectPtr<UTextBlock> SlotText;

	int64 ObjectId = 0;
	FString Name;
	FString SlotNames;

	bool bHovered = false;
	bool bSelected = false;
};
