#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/SWGExamineSubsystem.h"
#include "SWGHoloDetailCardWidget.generated.h"

class UBorder;
class UPanelWidget;
class UTextBlock;

/**
 * An item's examine details as a floating holo panel: name, where the item
 * is, description, and the server's attribute lines grouped under their
 * categories (USWGHoloAttributeLineWidget rows). Unfolds from its top edge
 * when shown.
 *
 * WBP_HoloDetailCard lays it out, binding any of Panel, NameText,
 * StatusText, DescriptionText, AttributeBox and FooterText; without one it
 * builds its own. It never takes the pointer.
 */
UCLASS(Blueprintable)
class SWGUI_API USWGHoloDetailCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** A card of the class set in Project Settings > SWG UI (HoloDetailCardClass), or this one. */
	static USWGHoloDetailCardWidget* Create(APlayerController* Owner);

	void SetInfo(const FSWGExamineInfo& Info);
	int64 GetObjectId() const { return ObjectId; }

	/** One line under the name saying where the item is ("Equipped • chest1", "In your inventory"). */
	void SetStatus(const FText& Status);

	/** Restarts the unfold. */
	void PlayOpen();

	/** Marks it as pinned open, with a note on how to let it go. */
	void SetPinned(bool bPinned);

	/** Widest the built-in layout grows, slate units; long descriptions wrap. A Blueprint sizes itself. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float CardWidth = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float UnfoldSeconds = 0.14f;

	/** Off to leave the panel's brush to the Blueprint. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	bool bApplyHoloStyle = true;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Panel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

	/** Filled with one attribute line per attribute and heading; a vertical box suits. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> AttributeBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FooterText;

private:
	int64 ObjectId = 0;
	float UnfoldAlpha = 1.f;
};
