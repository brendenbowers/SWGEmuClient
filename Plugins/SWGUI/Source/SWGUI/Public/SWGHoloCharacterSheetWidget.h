#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGHoloCharacterSheetWidget.generated.h"

class UTextBlock;
class USWGHoloAttributeLineWidget;
class USWGHoloMeterLineWidget;

/**
 * The holo inventory's character page: retail's character sheet Status and
 * Personal tabs (ui_pda_char_sheet) read off the local player — the nine
 * attributes with their wounds, modifiers and encumbrance, battle fatigue,
 * Force power, stomach, species, title and time played. Faction standing is
 * left out until FactionRequestMessage/FactionResponseMessage are decoded.
 *
 * WBP_HoloCharacterSheet, placed in WBP_HoloInventory, lays it out: every row and heading below is bound
 * by name, placed and styled in the designer (the bars' colours included);
 * this fills them with values and retail's strings.
 */
UCLASS(Abstract, Blueprintable)
class SWGUI_API USWGHoloCharacterSheetWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Re-reads the owning player into the rows. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloInventory")
	void Refresh();

	/** Off to leave the headings' fonts and colours to the Blueprint. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	bool bApplyHoloStyle = true;

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> AttributesHeading;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> HealthMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> StrengthMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> ConstitutionMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> ActionMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> QuicknessMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> StaminaMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> MindMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> FocusMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> WillpowerMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloAttributeLineWidget> FatigueLine;

	/** Hidden for characters with no Force power. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> ForceMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> StomachHeading;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> FoodMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloMeterLineWidget> DrinkMeter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> PersonalHeading;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloAttributeLineWidget> SpeciesLine;

	/** Hidden while the character has no title. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloAttributeLineWidget> TitleLine;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USWGHoloAttributeLineWidget> PlayedLine;

private:
	/** "@table:key" through the string tables, else the fallback. */
	FText Localized(const TCHAR* Table, const TCHAR* Key, const FText& Fallback) const;

	/** The nine meters in the CREO HAM order: health, strength, constitution, action, quickness, stamina, mind, focus, willpower. */
	TArray<USWGHoloMeterLineWidget*> AttributeMeters() const;

	/** The value rows' labels, looked up once. */
	FText FatigueLabel;
	FText SpeciesLabel;
	FText TitleLabel;
	FText PlayedLabel;
};
