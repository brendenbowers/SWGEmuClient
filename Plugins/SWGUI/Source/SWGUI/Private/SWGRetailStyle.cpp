#include "SWGRetailStyle.h"
#include "Components/Button.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Fonts/CompositeFont.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	/** ui_styles.inc SourceRects are x0,y0,x1,y1 with the max edge exclusive. DisplayScale shrinks the drawn slices (compact plate). */
	FSlateBrush MakeSheetBrush(UTexture2D* Texture, const FIntRect& SourceRect, const FMargin& MarginPixels, const FLinearColor& Tint, float DisplayScale = 1.f)
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
		// Box margins are fractions of the region, drawn at ImageSize pixels.
		Brush.ImageSize = FVector2D(RegionSize * DisplayScale);
		Brush.SetUVRegion(FBox2f(FVector2f(SourceRect.Min) / TextureSize, FVector2f(SourceRect.Max) / TextureSize));
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
	constexpr float CompactPlateScale = 0.5f;
	const FLinearColor HudDefaultTint = FLinearColor::FromSRGBColor(FColor(0x2D, 0xAA, 0xC6));
	const FLinearColor HudHoverTint = FLinearColor::FromSRGBColor(FColor(0x4F, 0xED, 0xFF));
	const FLinearColor HudActivatedTint = FLinearColor::FromSRGBColor(FColor(0x0B, 0xD7, 0xE8));
	const FLinearColor HudDisabledTint = FLinearColor::FromSRGBColor(FColor(0x01, 0x4A, 0x5A));

	const FIntRect CityPin(49, 350, 71, 369);
	const FLinearColor PinDisabledTint = FLinearColor::FromSRGBColor(FColor(0x00, 0xD6, 0xFB));

	/** A system TTF as a one-face composite font; null if the file isn't there. */
	TSharedPtr<const FCompositeFont> LoadSystemFont(const TCHAR* FileName)
	{
		const FString Path = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")), TEXT("Fonts"), FileName);
		if (!FPaths::FileExists(Path))
		{
			UE_LOG(LogTemp, Warning, TEXT("SWGRetailStyle: %s not found, falling back to the engine font"), *Path);
			return nullptr;
		}
		return MakeShared<const FCompositeFont>(TEXT("Regular"), Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	}

	/** Keeps a retail hud button at retail height inside whatever box the layout put it in. */
	void FitRetailHeight(UButton* Button)
	{
		if (UHorizontalBoxSlot* RowSlot = Cast<UHorizontalBoxSlot>(Button->Slot))
		{
			RowSlot->SetVerticalAlignment(VAlign_Center);
		}
		else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Button->Slot))
		{
			OverlaySlot->SetVerticalAlignment(VAlign_Center);
		}
		else if (UVerticalBoxSlot* ColumnSlot = Cast<UVerticalBoxSlot>(Button->Slot))
		{
			// A full-width stretch is the other way the plate distorts; retail sizes to the label.
			ColumnSlot->SetHorizontalAlignment(HAlign_Center);
			ColumnSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
}

namespace SWGRetailStyle
{
	FButtonStyle MakeHudButtonStyle(USWGTreSubsystem* Tre, bool bCompact, bool bTransparentIdle)
	{
		UTexture2D* Sheet = Tre ? Tre->GetOrLoadTexture(TEXT("texture/ui_rebel_final.dds")) : nullptr;
		const float Scale = bCompact ? CompactPlateScale : 1.f;
		FButtonStyle Style;
		Style.SetNormal(bTransparentIdle ? NoBrush() : MakeSheetBrush(Sheet, HudIdlePlate, HudPlateMargin, HudDefaultTint, Scale));
		FSlateBrush Hovered = MakeSheetBrush(Sheet, HudActivePlate, HudPlateMargin, HudHoverTint, Scale);
		if (bTransparentIdle)
		{
			// Behind a picture the full plate would bury it.
			Hovered.TintColor = FSlateColor(HudHoverTint.CopyWithNewOpacity(0.35f));
		}
		Style.SetHovered(Hovered);
		Style.SetPressed(MakeSheetBrush(Sheet, HudActivePlate, HudPlateMargin, HudActivatedTint, Scale));
		Style.SetDisabled(bTransparentIdle ? NoBrush() : MakeSheetBrush(Sheet, HudActivePlate, HudPlateMargin, HudDisabledTint, Scale));
		// ButtonStyle TextMargin='3,0,3,0', widened past the 11 px chamfer so the label clears it.
		const FMargin Padding = bCompact ? FMargin(4.f, 1.f) : FMargin(12.f, 1.f);
		Style.SetNormalPadding(Padding);
		Style.SetPressedPadding(Padding);
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

	FSlateFontInfo Font(int32 RetailPixelSize, bool bBold)
	{
		static const TSharedPtr<const FCompositeFont> Verdana = LoadSystemFont(TEXT("verdana.ttf"));
		static const TSharedPtr<const FCompositeFont> VerdanaBold = LoadSystemFont(TEXT("verdanab.ttf"));
		// Retail sizes are pixels; Slate sizes are points at 96 DPI.
		const float Points = RetailPixelSize * 0.75f;
		const TSharedPtr<const FCompositeFont>& Face = bBold ? VerdanaBold : Verdana;
		return Face ? FSlateFontInfo(Face, Points) : FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FMath::RoundToInt(Points));
	}

	UObject* ApplyHudButton(UButton* Button, USWGTreSubsystem* Tre, const FString& RetailLabel)
	{
		if (!Button)
		{
			return nullptr;
		}
		UTextBlock* Label = Cast<UTextBlock>(Button->GetContent());
		if (Label && !RetailLabel.IsEmpty() && Tre)
		{
			Label->SetText(FText::FromString(Tre->ResolveStringId(RetailLabel)));
		}
		const bool bCompact = Label && Label->GetText().ToString().Len() <= 2;
		Button->SetStyle(MakeHudButtonStyle(Tre, bCompact));
		Button->SetBackgroundColor(FLinearColor::White);
		FitRetailHeight(Button);
		if (!Label)
		{
			return nullptr;
		}
		Label->SetFont(Font(12));
		Label->SetJustification(ETextJustify::Center);
		USWGRetailButtonTextTint* Tint = NewObject<USWGRetailButtonTextTint>(Button);
		Tint->Button = Button;
		Tint->Label = Label;
		Button->OnHovered.AddDynamic(Tint, &USWGRetailButtonTextTint::HandleHovered);
		Button->OnUnhovered.AddDynamic(Tint, &USWGRetailButtonTextTint::HandleUnhovered);
		Button->OnPressed.AddDynamic(Tint, &USWGRetailButtonTextTint::HandlePressed);
		Button->OnReleased.AddDynamic(Tint, &USWGRetailButtonTextTint::HandleReleased);
		Tint->Refresh();
		return Tint;
	}
}

void USWGRetailButtonTextTint::Refresh()
{
	if (!Label.IsValid() || !Button.IsValid())
	{
		return;
	}
	bWasEnabled = Button->GetIsEnabled();
	const FLinearColor Color = !bWasEnabled ? SWGRetailStyle::HudTextDisabled
		: bPressed ? SWGRetailStyle::HudTextPressed
		: Button->IsHovered() ? SWGRetailStyle::HudTextHover
		: SWGRetailStyle::HudTextNormal;
	Label->SetColorAndOpacity(FSlateColor(Color));
}

void USWGRetailButtonTextTint::Tick(float DeltaTime)
{
	if (Button.IsValid() && Button->GetIsEnabled() != bWasEnabled)
	{
		Refresh();
	}
}

void USWGRetailButtonTextTint::HandleHovered() { Refresh(); }
void USWGRetailButtonTextTint::HandleUnhovered() { bPressed = false; Refresh(); }
void USWGRetailButtonTextTint::HandlePressed() { bPressed = true; Refresh(); }
void USWGRetailButtonTextTint::HandleReleased() { bPressed = false; Refresh(); }
