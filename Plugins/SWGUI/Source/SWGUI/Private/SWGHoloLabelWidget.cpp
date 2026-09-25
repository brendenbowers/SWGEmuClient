#include "SWGHoloLabelWidget.h"
#include "SWGHoloStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"

void USWGHoloLabelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetPadding(FMargin(8.f, 3.f));
		Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetFont(SWGHoloStyle::Font(12));
		Panel->SetContent(Label);
		WidgetTree->RootWidget = Panel;
	}
	SetVisibility(ESlateVisibility::Visible);
	ApplyStyle();
}

void USWGHoloLabelWidget::SetItem(int64 InObjectId, const FText& Text)
{
	ObjectId = InObjectId;
	if (Label)
	{
		Label->SetText(Text);
	}
}

void USWGHoloLabelWidget::SetWrapWidth(float Width)
{
	if (Label)
	{
		Label->SetWrapTextAt(Width);
	}
}

void USWGHoloLabelWidget::SetLit(bool bInLit)
{
	if (bLit == bInLit)
	{
		return;
	}
	bLit = bInLit;
	ApplyStyle();
}

void USWGHoloLabelWidget::ApplyStyle()
{
	const bool bBright = bHovered || bLit;
	if (Panel)
	{
		Panel->SetBrush(SWGHoloStyle::PanelBrush(bBright));
	}
	if (Label)
	{
		Label->SetColorAndOpacity(FSlateColor(bBright ? SWGHoloStyle::BrightText : SWGHoloStyle::Text));
	}
}

void USWGHoloLabelWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	bHovered = true;
	ApplyStyle();
	OnHovered.ExecuteIfBound(ObjectId, true);
}

void USWGHoloLabelWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bHovered = false;
	ApplyStyle();
	OnHovered.ExecuteIfBound(ObjectId, false);
}

FReply USWGHoloLabelWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	OnPressed.ExecuteIfBound(ObjectId, InMouseEvent.GetEffectingButton(), InMouseEvent.GetScreenSpacePosition());
	return FReply::Handled();
}
