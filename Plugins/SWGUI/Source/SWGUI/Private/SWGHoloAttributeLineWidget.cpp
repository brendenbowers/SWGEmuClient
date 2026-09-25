#include "SWGHoloAttributeLineWidget.h"
#include "SWGHoloStyle.h"
#include "SWGUISettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"

namespace
{
	constexpr float GroupIndent = 8.f;
}

USWGHoloAttributeLineWidget* USWGHoloAttributeLineWidget::Create(APlayerController* Owner)
{
	TSubclassOf<USWGHoloAttributeLineWidget> Class = USWGUISettings::Get().HoloAttributeLineClass.LoadSynchronous();
	return CreateWidget<USWGHoloAttributeLineWidget>(Owner, Class ? Class.Get() : StaticClass());
}

void USWGHoloAttributeLineWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree->RootWidget)
	{
		return;
	}
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	LabelText = WidgetTree->ConstructWidget<UTextBlock>();
	LabelText->SetFont(SWGHoloStyle::Font(11, false));
	ValueText = WidgetTree->ConstructWidget<UTextBlock>();
	ValueText->SetFont(SWGHoloStyle::Font(11, false));
	ValueText->SetJustification(ETextJustify::Right);
	if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelText))
	{
		LabelSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
	}
	if (UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(ValueText))
	{
		ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ValueSlot->SetHorizontalAlignment(HAlign_Right);
	}
	WidgetTree->RootWidget = Row;
}

void USWGHoloAttributeLineWidget::SetHeading(const FText& Heading)
{
	if (LabelText)
	{
		LabelText->SetText(Heading);
		if (bApplyHoloStyle)
		{
			LabelText->SetFont(SWGHoloStyle::Font(11));
			LabelText->SetColorAndOpacity(FSlateColor(SWGHoloStyle::Text));
			if (UHorizontalBoxSlot* LabelSlot = Cast<UHorizontalBoxSlot>(LabelText->Slot))
			{
				LabelSlot->SetPadding(FMargin(0.f, 4.f, 12.f, 1.f));
			}
		}
	}
	if (ValueText)
	{
		ValueText->SetVisibility(ESlateVisibility::Collapsed);
	}
	OnLineSet(/*bHeading=*/true, /*bInGroup=*/false);
}

void USWGHoloAttributeLineWidget::SetAttribute(const FText& Label, const FText& Value, bool bInGroup)
{
	if (LabelText)
	{
		LabelText->SetText(Label);
		if (bApplyHoloStyle)
		{
			LabelText->SetFont(SWGHoloStyle::Font(11, false));
			LabelText->SetColorAndOpacity(FSlateColor(SWGHoloStyle::DimText));
			if (UHorizontalBoxSlot* LabelSlot = Cast<UHorizontalBoxSlot>(LabelText->Slot))
			{
				LabelSlot->SetPadding(FMargin(bInGroup ? GroupIndent : 0.f, 0.f, 12.f, 0.f));
			}
		}
	}
	if (ValueText)
	{
		ValueText->SetText(Value);
		ValueText->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (bApplyHoloStyle)
		{
			ValueText->SetFont(SWGHoloStyle::Font(11, false));
			ValueText->SetColorAndOpacity(FSlateColor(SWGHoloStyle::BrightText));
		}
	}
	OnLineSet(/*bHeading=*/false, bInGroup);
}
