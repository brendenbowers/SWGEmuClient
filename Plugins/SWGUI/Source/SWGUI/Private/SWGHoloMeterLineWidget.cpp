#include "SWGHoloMeterLineWidget.h"
#include "SWGHoloStyle.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

namespace
{
	const FLinearColor NoteColor(1.f, 0.55f, 0.45f);
}

void USWGHoloMeterLineWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	if (bApplyHoloStyle)
	{
		// The holo font is a system face, not an asset, so a Blueprint can't pick it.
		auto Style = [](UTextBlock* Text, int32 Size, const FLinearColor& Color)
		{
			Text->SetFont(SWGHoloStyle::Font(Size, false));
			Text->SetColorAndOpacity(FSlateColor(Color));
		};
		Style(LabelText, 11, SWGHoloStyle::DimText);
		Style(ValueText, 11, SWGHoloStyle::BrightText);
		Style(NoteText, 10, NoteColor);
		// A white fill, so FillColor shows true.
		Bar->SetWidgetStyle(SWGHoloStyle::BarStyle(FLinearColor::White));
	}
	Bar->SetFillColorAndOpacity(FillColor);
	if (!IsDesignTime())
	{
		NoteText->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void USWGHoloMeterLineWidget::SetLabel(const FText& Label)
{
	LabelText->SetText(Label);
}

void USWGHoloMeterLineWidget::SetMeter(int32 Current, int32 Max, const FText& Note)
{
	Bar->SetPercent(Max > 0 ? FMath::Clamp(float(Current) / Max, 0.f, 1.f) : 0.f);
	ValueText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "CharSheetMeter", "{0} / {1}"), FText::AsNumber(Current), FText::AsNumber(Max)));
	NoteText->SetText(Note);
	NoteText->SetVisibility(Note.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}
