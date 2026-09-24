#include "SWGRetailStyle.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	/** ui_styles.inc SourceRects are x0,y0,x1,y1 with the max edge exclusive. */
	FSlateBrush MakeSheetBrush(UTexture2D* Texture, const FIntRect& SourceRect, const FMargin& MarginPixels, const FLinearColor& Tint)
	{
		FSlateBrush Brush;
		if (!Texture)
		{
			Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
			return Brush;
		}
		const FVector2f TextureSize(Texture->GetSizeX(), Texture->GetSizeY());
		const FVector2f RegionSize(SourceRect.Width(), SourceRect.Height());
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(RegionSize);
		Brush.SetUVRegion(FBox2f(FVector2f(SourceRect.Min) / TextureSize, FVector2f(SourceRect.Max) / TextureSize));
		// Slate's box margins are fractions of the region.
		Brush.Margin = FMargin(MarginPixels.Left / RegionSize.X, MarginPixels.Top / RegionSize.Y,
			MarginPixels.Right / RegionSize.X, MarginPixels.Bottom / RegionSize.Y);
		Brush.DrawAs = MarginPixels.Left > 0.f ? ESlateBrushDrawType::Box : ESlateBrushDrawType::Image;
		Brush.TintColor = FSlateColor(Tint);
		return Brush;
	}

	FSlateBrush NoBrush()
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
		return Brush;
	}

	// New.buttons.hud: the idle state uses its own plate, every other state the
	// solid one beside it. Both nine-slice with 11 px ends and 5 px caps.
	const FIntRect HudIdlePlate(483, 224, 509, 237);
	const FIntRect HudActivePlate(457, 226, 482, 237);
	const FMargin HudPlateMargin(11.f, 5.f, 11.f, 5.f);
	const FLinearColor HudDefaultTint = FLinearColor::FromSRGBColor(FColor(0x2D, 0xAA, 0xC6));
	const FLinearColor HudHoverTint = FLinearColor::FromSRGBColor(FColor(0x4F, 0xED, 0xFF));
	const FLinearColor HudActivatedTint = FLinearColor::FromSRGBColor(FColor(0x0B, 0xD7, 0xE8));
	const FLinearColor HudDisabledTint = FLinearColor::FromSRGBColor(FColor(0x01, 0x4A, 0x5A));

	const FIntRect CityPin(49, 350, 71, 369);
	const FLinearColor PinDisabledTint = FLinearColor::FromSRGBColor(FColor(0x00, 0xD6, 0xFB));
}

namespace SWGRetailStyle
{
	FButtonStyle MakeHudButtonStyle(USWGTreSubsystem* Tre, bool bTransparentIdle)
	{
		UTexture2D* Sheet = Tre ? Tre->GetOrLoadTexture(TEXT("texture/ui_rebel_final.dds")) : nullptr;
		FButtonStyle Style;
		Style.SetNormal(bTransparentIdle ? NoBrush() : MakeSheetBrush(Sheet, HudIdlePlate, HudPlateMargin, HudDefaultTint));
		FSlateBrush Hovered = MakeSheetBrush(Sheet, HudActivePlate, HudPlateMargin, HudHoverTint);
		if (bTransparentIdle)
		{
			// Behind a picture the full plate would bury it.
			Hovered.TintColor = FSlateColor(HudHoverTint.CopyWithNewOpacity(0.35f));
		}
		Style.SetHovered(Hovered);
		Style.SetPressed(MakeSheetBrush(Sheet, HudActivePlate, HudPlateMargin, HudActivatedTint));
		Style.SetDisabled(bTransparentIdle ? NoBrush() : MakeSheetBrush(Sheet, HudActivePlate, HudPlateMargin, HudDisabledTint));
		// ButtonStyle *TextMargin='3,0,3,0'.
		Style.SetNormalPadding(FMargin(3.f, 0.f));
		Style.SetPressedPadding(FMargin(3.f, 0.f));
		return Style;
	}

	FButtonStyle MakePinStyle(USWGTreSubsystem* Tre, const FLinearColor& IdleColor)
	{
		UTexture2D* Sheet = Tre ? Tre->GetOrLoadTexture(TEXT("texture/ui_rebel_icons.dds")) : nullptr;
		FButtonStyle Style;
		Style.SetNormal(MakeSheetBrush(Sheet, CityPin, FMargin(0.f), IdleColor));
		Style.SetHovered(MakeSheetBrush(Sheet, CityPin, FMargin(0.f), PinHover));
		Style.SetPressed(MakeSheetBrush(Sheet, CityPin, FMargin(0.f), PinActivated));
		Style.SetDisabled(MakeSheetBrush(Sheet, CityPin, FMargin(0.f), PinDisabledTint));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		return Style;
	}

	FSlateFontInfo BoldFont(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Bold", Size);
	}

	UObject* ApplyHudButton(UButton* Button, USWGTreSubsystem* Tre, bool bTransparentIdle)
	{
		if (!Button)
		{
			return nullptr;
		}
		Button->SetStyle(MakeHudButtonStyle(Tre, bTransparentIdle));
		Button->SetBackgroundColor(FLinearColor::White);
		UTextBlock* Label = Cast<UTextBlock>(Button->GetContent());
		if (!Label)
		{
			return nullptr;
		}
		FSlateFontInfo Font = Label->GetFont();
		Font.TypefaceFontName = TEXT("Bold");
		Label->SetFont(Font);
		Label->SetColorAndOpacity(FSlateColor(HudTextNormal));
		USWGRetailButtonTextTint* Tint = NewObject<USWGRetailButtonTextTint>(Button);
		Tint->Button = Button;
		Tint->Label = Label;
		Button->OnHovered.AddDynamic(Tint, &USWGRetailButtonTextTint::HandleHovered);
		Button->OnUnhovered.AddDynamic(Tint, &USWGRetailButtonTextTint::HandleUnhovered);
		Button->OnPressed.AddDynamic(Tint, &USWGRetailButtonTextTint::HandlePressed);
		Button->OnReleased.AddDynamic(Tint, &USWGRetailButtonTextTint::HandleReleased);
		return Tint;
	}
}

void USWGRetailButtonTextTint::HandleHovered()
{
	if (Label.IsValid())
	{
		Label->SetColorAndOpacity(FSlateColor(SWGRetailStyle::HudTextHover));
	}
}

void USWGRetailButtonTextTint::HandleUnhovered()
{
	if (Label.IsValid())
	{
		Label->SetColorAndOpacity(FSlateColor(SWGRetailStyle::HudTextNormal));
	}
}

void USWGRetailButtonTextTint::HandlePressed()
{
	if (Label.IsValid())
	{
		Label->SetColorAndOpacity(FSlateColor(SWGRetailStyle::HudTextPressed));
	}
}

void USWGRetailButtonTextTint::HandleReleased()
{
	if (Label.IsValid())
	{
		Label->SetColorAndOpacity(FSlateColor(Button.IsValid() && Button->IsHovered()
			? SWGRetailStyle::HudTextHover : SWGRetailStyle::HudTextNormal));
	}
}
