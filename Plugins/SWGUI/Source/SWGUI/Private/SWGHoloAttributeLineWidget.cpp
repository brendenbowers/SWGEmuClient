#include "SWGHoloAttributeLineWidget.h"
#include "SWGHoloStyle.h"
#include "SWGUISettings.h"
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
	if (!Class)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGHoloAttributeLineWidget: HoloAttributeLineClass is unset (Project Settings > SWG UI)."));
		return nullptr;
	}
	return CreateWidget<USWGHoloAttributeLineWidget>(Owner, Class);
}

void USWGHoloAttributeLineWidget::SetHeading(const FText& Heading)
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
	ValueText->SetVisibility(ESlateVisibility::Collapsed);
	OnLineSet(/*bHeading=*/true, /*bInGroup=*/false);
}

void USWGHoloAttributeLineWidget::SetAttribute(const FText& Label, const FText& Value, bool bInGroup)
{
	LabelText->SetText(Label);
	ValueText->SetText(Value);
	ValueText->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (bApplyHoloStyle)
	{
		// The holo font is a system face, not an asset, so a Blueprint can't pick it.
		LabelText->SetFont(SWGHoloStyle::Font(11, false));
		LabelText->SetColorAndOpacity(FSlateColor(SWGHoloStyle::DimText));
		ValueText->SetFont(SWGHoloStyle::Font(11, false));
		ValueText->SetColorAndOpacity(FSlateColor(SWGHoloStyle::BrightText));
		if (UHorizontalBoxSlot* LabelSlot = Cast<UHorizontalBoxSlot>(LabelText->Slot))
		{
			LabelSlot->SetPadding(FMargin(bInGroup ? GroupIndent : 0.f, 0.f, 12.f, 0.f));
		}
	}
	OnLineSet(/*bHeading=*/false, bInGroup);
}
