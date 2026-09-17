#include "SWGInventoryRowWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "ModelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

TSharedRef<SWidget> USWGInventoryRowWidget::RebuildWidget()
{
	if (!WidgetTree->RootWidget)
	{
		Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetPadding(FMargin(4.f, 2.f));
		WidgetTree->RootWidget = Background;

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Background->AddChild(Row);

		USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>();
		IconBox->SetWidthOverride(IconSize);
		IconBox->SetHeightOverride(IconSize);
		Model = WidgetTree->ConstructWidget<UModelWidget>();
		Model->DesiredSize = FVector2D(IconSize, IconSize);
		Model->Fill = 0.85f;
		Model->SetVisibility(ESlateVisibility::HitTestInvisible);
		IconBox->AddChild(Model);
		UHorizontalBoxSlot* IconSlot = Row->AddChildToHorizontalBox(IconBox);
		IconSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		IconSlot->SetVerticalAlignment(VAlign_Center);

		UVerticalBox* Labels = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Labels);
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetVerticalAlignment(VAlign_Center);

		NameText = WidgetTree->ConstructWidget<UTextBlock>();
		NameText->SetColorAndOpacity(FSlateColor(NameColor));
		FSlateFontInfo Font = NameText->GetFont();
		Font.Size = FontSize;
		NameText->SetFont(Font);
		Labels->AddChildToVerticalBox(NameText);

		SlotText = WidgetTree->ConstructWidget<UTextBlock>();
		SlotText->SetColorAndOpacity(FSlateColor(SlotColor));
		Font.Size = FontSize - 3;
		SlotText->SetFont(Font);
		SlotText->SetVisibility(ESlateVisibility::Collapsed);
		Labels->AddChildToVerticalBox(SlotText);

		ApplyBackground();
		ApplyRow();
	}
	return Super::RebuildWidget();
}

void USWGInventoryRowWidget::SetRow(int64 InObjectId, const FString& InName, const FString& InSlotNames)
{
	ObjectId = InObjectId;
	Name = InName;
	SlotNames = InSlotNames;
	ApplyRow();
}

void USWGInventoryRowWidget::ApplyRow()
{
	// Set before the tree exists (SetRow follows CreateWidget; the tree is
	// built when the row is first added to a panel), so this runs from both.

	if (NameText)
	{
		NameText->SetText(FText::FromString(Name));
	}
	if (SlotText)
	{
		SlotText->SetText(FText::FromString(SlotNames));
		SlotText->SetVisibility(SlotNames.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (Model)
	{
		Model->SetObject(ObjectId);
	}
}

void USWGInventoryRowWidget::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;
	ApplyBackground();
}

void USWGInventoryRowWidget::ApplyBackground()
{
	if (Background)
	{
		Background->SetBrushColor(bSelected ? SelectedColor : (bHovered ? HoverColor : FLinearColor::Transparent));
	}
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
	if (Model)
	{
		Model->SetRotateSpeed(HoverRotateSpeed);
	}
}

void USWGInventoryRowWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bHovered = false;
	ApplyBackground();
	if (Model)
	{
		Model->SetRotateSpeed(0.f);
	}
}
