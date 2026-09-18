#include "SWGInventoryRowWidget.h"
#include "Components/Border.h"
#include "ModelWidget.h"
#include "Components/TextBlock.h"

void USWGInventoryRowWidget::SetRow(int64 InObjectId, const FString& Name, const FString& SlotNames)
{
	ObjectId = InObjectId;
	NameText->SetText(FText::FromString(Name));
	SlotText->SetText(FText::FromString(SlotNames));
	SlotText->SetVisibility(SlotNames.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	Model->SetObject(ObjectId);
}

void USWGInventoryRowWidget::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;
	ApplyBackground();
}

void USWGInventoryRowWidget::ApplyBackground()
{
	Background->SetBrushColor(bSelected ? SelectedColor : (bHovered ? HoverColor : FLinearColor::Transparent));
}

FReply USWGInventoryRowWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	OnPressed.ExecuteIfBound(ObjectId, InMouseEvent.GetEffectingButton(), InMouseEvent.GetScreenSpacePosition());
	return FReply::Handled();
}

void USWGInventoryRowWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	bHovered = true;
	ApplyBackground();
	Model->SetRotateSpeed(HoverRotateSpeed);
}

void USWGInventoryRowWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bHovered = false;
	ApplyBackground();
	Model->SetRotateSpeed(0.f);
}
