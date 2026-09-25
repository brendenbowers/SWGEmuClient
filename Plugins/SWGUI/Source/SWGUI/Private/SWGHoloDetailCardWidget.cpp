#include "SWGHoloDetailCardWidget.h"
#include "SWGHoloStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	UTextBlock* MakeText(UWidgetTree* Tree, int32 Size, const FLinearColor& Color, bool bBold = true)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetFont(SWGHoloStyle::Font(Size, bBold));
		Text->SetColorAndOpacity(FSlateColor(Color));
		return Text;
	}
}

void USWGHoloDetailCardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree->RootWidget)
	{
		return;
	}
	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
	Width->SetMaxDesiredWidth(CardWidth);
	Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrush(SWGHoloStyle::PanelBrush(true));
	Panel->SetPadding(FMargin(10.f, 8.f));
	Width->SetContent(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Column);
	NameText = MakeText(WidgetTree, 14, SWGHoloStyle::BrightText);
	NameText->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(NameText);
	StatusText = MakeText(WidgetTree, 10, SWGHoloStyle::Text);
	StatusText->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(StatusText);
	DescriptionText = MakeText(WidgetTree, 11, SWGHoloStyle::DimText, false);
	DescriptionText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* DescriptionSlot = Column->AddChildToVerticalBox(DescriptionText))
	{
		DescriptionSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}
	AttributeBox = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UVerticalBoxSlot* AttributeSlot = Column->AddChildToVerticalBox(AttributeBox))
	{
		AttributeSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}
	FooterText = MakeText(WidgetTree, 10, SWGHoloStyle::DimText, false);
	FooterText->SetText(NSLOCTEXT("SWGEmu", "HoloCardPinned", "Pinned  •  click the name again to release"));
	FooterText->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* FooterSlot = Column->AddChildToVerticalBox(FooterText))
	{
		FooterSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}
	WidgetTree->RootWidget = Width;
	// Details only; the pointer passes through to the names and the hologram.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void USWGHoloDetailCardWidget::SetInfo(const FSWGExamineInfo& Info)
{
	ObjectId = Info.ObjectId;
	NameText->SetText(FText::FromString(Info.Name));
	DescriptionText->SetText(FText::FromString(Info.Description));
	DescriptionText->SetVisibility(Info.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	AttributeBox->ClearChildren();
	FString Category;
	for (const FSWGExamineAttribute& Attribute : Info.Attributes)
	{
		// A header wherever the group changes, as retail's examine window lays them out.
		if (!Attribute.Category.IsEmpty() && Attribute.Category != Category)
		{
			UTextBlock* Header = MakeText(WidgetTree, 11, SWGHoloStyle::Text);
			Header->SetText(FText::FromString(Attribute.Category));
			if (UVerticalBoxSlot* HeaderSlot = AttributeBox->AddChildToVerticalBox(Header))
			{
				HeaderSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 1.f));
			}
		}
		Category = Attribute.Category;
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UTextBlock* Label = MakeText(WidgetTree, 11, SWGHoloStyle::DimText, false);
		Label->SetText(FText::FromString(Attribute.Label));
		UTextBlock* Value = MakeText(WidgetTree, 11, SWGHoloStyle::BrightText, false);
		Value->SetText(FText::FromString(Attribute.Value));
		Value->SetJustification(ETextJustify::Right);
		if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label))
		{
			LabelSlot->SetPadding(FMargin(Attribute.Category.IsEmpty() ? 0.f : 8.f, 0.f, 12.f, 0.f));
		}
		if (UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(Value))
		{
			ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			ValueSlot->SetHorizontalAlignment(HAlign_Right);
		}
		AttributeBox->AddChildToVerticalBox(Row);
	}
}

void USWGHoloDetailCardWidget::SetStatus(const FText& Status)
{
	StatusText->SetText(Status);
	StatusText->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void USWGHoloDetailCardWidget::SetPinned(bool bPinned)
{
	FooterText->SetVisibility(bPinned ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void USWGHoloDetailCardWidget::PlayOpen()
{
	UnfoldAlpha = 0.f;
	SetRenderTransformPivot(FVector2D(0.5f, 0.f));
	SetRenderScale(FVector2D(1.f, 0.f));
	SetRenderOpacity(0.f);
}

void USWGHoloDetailCardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (UnfoldAlpha >= 1.f)
	{
		return;
	}
	UnfoldAlpha = FMath::Min(1.f, UnfoldAlpha + InDeltaTime / FMath::Max(UnfoldSeconds, 0.01f));
	const float Eased = FMath::InterpEaseOut(0.f, 1.f, UnfoldAlpha, 2.f);
	SetRenderScale(FVector2D(1.f, Eased));
	SetRenderOpacity(Eased);
}
