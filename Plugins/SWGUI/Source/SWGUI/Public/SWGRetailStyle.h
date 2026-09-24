#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "UObject/Object.h"
#include "SWGRetailStyle.generated.h"

class UButton;
class UTextBlock;
class USWGTreSubsystem;

/**
 * Looks lifted from the retail client's ui/ui_styles.inc, drawn from the same
 * sprite sheets (texture/ui_rebel_final.dds, ui_rebel_icons.dds) with the
 * same per-state tints, and set in Verdana as retail's bitmap fonts were.
 */
namespace SWGRetailStyle
{
	/** /Styles.New.buttons.hud.style text colours. */
	const FLinearColor HudTextNormal = FLinearColor::FromSRGBColor(FColor(0x2F, 0xF4, 0xFF));
	const FLinearColor HudTextHover = FLinearColor::FromSRGBColor(FColor(0x18, 0x39, 0x3D));
	const FLinearColor HudTextPressed = FLinearColor::FromSRGBColor(FColor(0x00, 0x39, 0x3E));
	const FLinearColor HudTextDisabled = FLinearColor::FromSRGBColor(FColor(0x02, 0x8B, 0xA8));

	/** /Styles.New.buttons.city: the travel-point pin and its label (ui_ticketpurchase textSample). */
	const FLinearColor PinDefault = FLinearColor::FromSRGBColor(FColor(0xFF, 0x74, 0x17));
	const FLinearColor PinHover = FLinearColor::FromSRGBColor(FColor(0x62, 0xFF, 0x15));
	const FLinearColor PinActivated = FLinearColor::FromSRGBColor(FColor(0x00, 0xFF, 0xFF));
	const FLinearColor PinLabel = FLinearColor::FromSRGBColor(FColor(0x62, 0xFF, 0x15));
	const FVector2D PinSize(21.f, 17.f);

	/** Every hud button in ui_ticketpurchase.inc is 19 px tall. */
	constexpr float HudButtonHeight = 19.f;

	/**
	 * /Styles.New.buttons.hud.style: the chamfered plate retail uses for
	 * window buttons (Galaxy, Purchase, Exit). bCompact halves the plate for
	 * buttons too narrow for its 11 px ends (a "+" or "-"). bTransparentIdle
	 * drops the idle plate, for buttons whose content is the picture.
	 */
	SWGUI_API FButtonStyle MakeHudButtonStyle(USWGTreSubsystem* Tre, bool bCompact = false, bool bTransparentIdle = false);

	/** /Styles.New.buttons.city.style: the map pin, idle in IdleColor. */
	SWGUI_API FButtonStyle MakePinStyle(USWGTreSubsystem* Tre, const FLinearColor& IdleColor);

	/**
	 * Styles Button as a retail hud button: plate, Verdana bold 12, per-state
	 * text colours (which UButton can't do alone) and retail's 19 px height
	 * where its slot allows. A non-empty RetailLabel ("@ui:purchase") replaces
	 * the text with retail's own string. Keep the returned object alive as
	 * long as the button (a UPROPERTY on the owner); null without a text label.
	 */
	SWGUI_API UObject* ApplyHudButton(UButton* Button, USWGTreSubsystem* Tre, const FString& RetailLabel = FString());

	/**
	 * Retail's Verdana at one of its bitmap sizes ("bold_12" = 12 px). Loaded
	 * from the system's Verdana; the engine's default face if it's missing.
	 */
	SWGUI_API FSlateFontInfo Font(int32 RetailPixelSize, bool bBold = true);
}

/**
 * Recolours one button's label as retail does on hover, press and disable.
 * UButton reports no enable change, so it watches that per frame.
 */
UCLASS()
class USWGRetailButtonTextTint : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UButton> Button;
	TWeakObjectPtr<UTextBlock> Label;

	void Refresh();

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(USWGRetailButtonTextTint, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate() && Button.IsValid(); }

	UFUNCTION() void HandleHovered();
	UFUNCTION() void HandleUnhovered();
	UFUNCTION() void HandlePressed();
	UFUNCTION() void HandleReleased();

private:
	bool bPressed = false;
	bool bWasEnabled = true;
};
