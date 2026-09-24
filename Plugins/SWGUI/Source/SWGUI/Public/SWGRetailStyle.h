#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "UObject/Object.h"
#include "SWGRetailStyle.generated.h"

class UButton;
class UTextBlock;
class USWGTreSubsystem;

/**
 * Button looks lifted from the retail client's ui/ui_styles.inc, drawn from
 * the same sprite sheets (texture/ui_rebel_final.dds, ui_rebel_icons.dds)
 * with the same per-state tints.
 */
namespace SWGRetailStyle
{
	/** /Styles.New.buttons.hud.style text colours. */
	const FLinearColor HudTextNormal = FLinearColor::FromSRGBColor(FColor(0x2F, 0xF4, 0xFF));
	const FLinearColor HudTextHover = FLinearColor::FromSRGBColor(FColor(0x18, 0x39, 0x3D));
	const FLinearColor HudTextPressed = FLinearColor::FromSRGBColor(FColor(0x00, 0x39, 0x3E));

	/** /Styles.New.buttons.city: the travel-point pin and its label (ui_ticketpurchase textSample). */
	const FLinearColor PinDefault = FLinearColor::FromSRGBColor(FColor(0xFF, 0x74, 0x17));
	const FLinearColor PinHover = FLinearColor::FromSRGBColor(FColor(0x62, 0xFF, 0x15));
	const FLinearColor PinActivated = FLinearColor::FromSRGBColor(FColor(0x00, 0xFF, 0xFF));
	const FLinearColor PinLabel = FLinearColor::FromSRGBColor(FColor(0x62, 0xFF, 0x15));
	const FVector2D PinSize(21.f, 17.f);

	/**
	 * /Styles.New.buttons.hud.style: the chamfered plate retail uses for
	 * window buttons (Galaxy, Purchase, Exit). bTransparentIdle drops the idle
	 * plate, for buttons whose content is the picture (planet icons).
	 */
	SWGUI_API FButtonStyle MakeHudButtonStyle(USWGTreSubsystem* Tre, bool bTransparentIdle = false);

	/** /Styles.New.buttons.city.style: the map pin, idle in IdleColor. */
	SWGUI_API FButtonStyle MakePinStyle(USWGTreSubsystem* Tre, const FLinearColor& IdleColor);

	/**
	 * Styles Button as a hud button and recolours its text content per state,
	 * which UButton can't do on its own. Keep the returned object alive as long
	 * as the button (a UPROPERTY on the owning widget); null if there's no text.
	 */
	SWGUI_API UObject* ApplyHudButton(UButton* Button, USWGTreSubsystem* Tre, bool bTransparentIdle = false);

	/** Retail's bold UI face at Size points. */
	SWGUI_API FSlateFontInfo BoldFont(int32 Size);
}

/** Recolours one button's label as retail does on hover and press. */
UCLASS()
class USWGRetailButtonTextTint : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UButton> Button;
	TWeakObjectPtr<UTextBlock> Label;

	UFUNCTION() void HandleHovered();
	UFUNCTION() void HandleUnhovered();
	UFUNCTION() void HandlePressed();
	UFUNCTION() void HandleReleased();
};
