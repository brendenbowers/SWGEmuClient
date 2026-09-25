#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGHoloMeterLineWidget.generated.h"

class UProgressBar;
class UTextBlock;

/**
 * One meter on the holo character sheet, laid out by WBP_HoloMeterLine: a
 * name, a bar, "current / max", and a note under them (wounds, modifier,
 * encumbrance) that shows only when it has something to say.
 */
UCLASS(Abstract, Blueprintable)
class SWGUI_API USWGHoloMeterLineWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetLabel(const FText& Label);
	void SetMeter(int32 Current, int32 Max, const FText& Note);

	/** The bar's colour; set per row where the sheet places it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Hologram")
	FLinearColor FillColor = FLinearColor::White;

	/** Off to leave fonts, colours and the bar's brushes to the Blueprint. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Hologram")
	bool bApplyHoloStyle = true;

protected:
	virtual void NativePreConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UProgressBar> Bar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> ValueText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> NoteText;
};
