#include "SWGExamineLineWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"

void USWGExamineLineWidget::CaptureLabelColor()
{
	if (!bLabelColorCaptured)
	{
		LineLabelColor = LabelText->GetColorAndOpacity();
		bLabelColorCaptured = true;
	}
}

void USWGExamineLineWidget::SetLine(const FText& Label, const FText& Value, bool bIndented)
{
	CaptureLabelColor();
	LabelText->SetColorAndOpacity(LineLabelColor);
	LabelText->SetText(FText::Format(NSLOCTEXT("SWGExamine", "LineLabel", "{0}:"), Label));
	ValueText->SetText(Value);
	ValueText->SetVisibility(ESlateVisibility::HitTestInvisible);
	Indent->SetVisibility(bIndented ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* ListSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		ListSlot->SetPadding(FMargin(0.f));
	}
}

void USWGExamineLineWidget::SetHeader(const FText& Category)
{
	CaptureLabelColor();
	LabelText->SetColorAndOpacity(FSlateColor(HeaderColor));
	LabelText->SetText(Category);
	ValueText->SetVisibility(ESlateVisibility::Collapsed);
	Indent->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* ListSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		ListSlot->SetPadding(HeaderPadding);
	}
}
