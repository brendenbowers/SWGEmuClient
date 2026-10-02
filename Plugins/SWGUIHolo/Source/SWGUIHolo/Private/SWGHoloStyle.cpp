#include "SWGHoloStyle.h"
#include "SWGRetailStyle.h"

FSlateFontInfo SWGHoloStyle::Font(int32 Size, bool bBold)
{
	return SWGRetailStyle::Font(Size, bBold);
}

FSlateBrush SWGHoloStyle::PanelBrush(bool bLit)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(bLit ? FLinearColor(0.03f, 0.2f, 0.3f, 0.85f) : FLinearColor(0.02f, 0.12f, 0.18f, 0.6f));
	Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(3.f, 3.f, 3.f, 3.f), FSlateColor(bLit ? BrightLine : Line), 1.f);
	return Brush;
}

FSlateBrush SWGHoloStyle::FrameBrush()
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(FLinearColor(0.02f, 0.15f, 0.22f, 0.12f));
	Brush.OutlineSettings = FSlateBrushOutlineSettings(FVector4(4.f, 4.f, 4.f, 4.f), FSlateColor(Line * FLinearColor(1.f, 1.f, 1.f, 0.6f)), 1.f);
	return Brush;
}

FProgressBarStyle SWGHoloStyle::BarStyle(const FLinearColor& FillColor)
{
	FSlateBrush Track;
	Track.DrawAs = ESlateBrushDrawType::RoundedBox;
	// Retail's bar page: #03546B at half opacity.
	Track.TintColor = FSlateColor(FLinearColor::FromSRGBColor(FColor(0x03, 0x54, 0x6B, 0x80)));
	Track.OutlineSettings = FSlateBrushOutlineSettings(FVector4(4.f, 4.f, 4.f, 4.f), FSlateColor(Line), 1.f);
	FSlateBrush Fill;
	Fill.DrawAs = ESlateBrushDrawType::RoundedBox;
	Fill.TintColor = FSlateColor(FillColor * FLinearColor(1.f, 1.f, 1.f, 0.9f));
	Fill.OutlineSettings = FSlateBrushOutlineSettings(FVector4(4.f, 4.f, 4.f, 4.f), FSlateColor(FLinearColor::Transparent), 0.f);
	FProgressBarStyle Style;
	Style.SetBackgroundImage(Track);
	Style.SetFillImage(Fill);
	Style.SetMarqueeImage(Fill);
	return Style;
}

FButtonStyle SWGHoloStyle::ChipStyle()
{
	FButtonStyle Style;
	Style.SetNormal(PanelBrush(false));
	Style.SetHovered(PanelBrush(true));
	Style.SetPressed(PanelBrush(true));
	Style.SetNormalPadding(FMargin(8.f, 1.f));
	Style.SetPressedPadding(FMargin(8.f, 2.f, 8.f, 0.f));
	return Style;
}
