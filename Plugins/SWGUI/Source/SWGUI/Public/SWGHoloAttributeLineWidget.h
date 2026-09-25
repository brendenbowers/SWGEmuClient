#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGHoloAttributeLineWidget.generated.h"

class UTextBlock;

/**
 * One line of a holo detail card: an attribute ("Min Damage    110") or the
 * heading of a group of them. WBP_HoloAttributeLine lays it out, binding
 * LabelText and ValueText; without one it builds its own. OnLineSet tells a
 * Blueprint which kind it is showing, for styling.
 */
UCLASS(Blueprintable)
class SWGUI_API USWGHoloAttributeLineWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** A line of the class set in Project Settings > SWG UI (HoloAttributeLineClass), or this one. */
	static USWGHoloAttributeLineWidget* Create(APlayerController* Owner);

	void SetHeading(const FText& Heading);

	/** bInGroup indents it under the heading above. */
	void SetAttribute(const FText& Label, const FText& Value, bool bInGroup);

	UFUNCTION(BlueprintImplementableEvent, Category = "SWGEmu|Hologram")
	void OnLineSet(bool bHeading, bool bInGroup);

	/** Off to leave colours and indent to the Blueprint (OnLineSet). */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Hologram")
	bool bApplyHoloStyle = true;

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ValueText;
};
