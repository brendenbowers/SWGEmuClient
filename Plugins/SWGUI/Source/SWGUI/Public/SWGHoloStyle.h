#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

/** The look of screen-space UI that belongs to a hologram: cyan text and translucent panels with a glowing edge. */
namespace SWGHoloStyle
{
	/** Retail's HUD hint cyan. */
	inline const FLinearColor Text = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));
	inline const FLinearColor BrightText = FLinearColor(0.85f, 1.f, 1.f);
	inline const FLinearColor Line = FLinearColor(0.35f, 0.85f, 1.f, 0.55f);
	inline const FLinearColor BrightLine = FLinearColor(0.7f, 1.f, 1.f, 0.95f);
	/** Secondary text: attribute labels, descriptions. */
	inline const FLinearColor DimText = FLinearColor(0.45f, 0.75f, 0.85f);

	SWGUI_API FSlateFontInfo Font(int32 Size, bool bBold = true);

	/** A rounded translucent fill with a thin cyan edge; brighter when lit. */
	SWGUI_API FSlateBrush PanelBrush(bool bLit);

	/** A thin cyan edge round a nearly clear fill, for framing things drawn in the world behind it. */
	SWGUI_API FSlateBrush FrameBrush();

	/** Retail's meter: a pill of FillColor over a dim teal track (ui_pda_inventory's capacity bar). */
	SWGUI_API FProgressBarStyle BarStyle(const FLinearColor& FillColor);

	/** A small holo button, lit while hovered. */
	SWGUI_API FButtonStyle ChipStyle();

	/** Retail's capacity bar fill (PalColor "exp"). */
	inline const FLinearColor CapacityFill = FLinearColor::FromSRGBColor(FColor(0x6C, 0xFD, 0x02));
	inline const FLinearColor FullFill = FLinearColor(1.f, 0.25f, 0.15f);
}
