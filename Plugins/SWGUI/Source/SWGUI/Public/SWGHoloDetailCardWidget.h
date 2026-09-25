#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/SWGExamineSubsystem.h"
#include "SWGHoloDetailCardWidget.generated.h"

class UBorder;
class UTextBlock;
class UVerticalBox;

/**
 * An item's examine details as a floating holo panel: name, description and
 * the server's attribute lines grouped under their categories. Unfolds from
 * its top edge when shown.
 */
UCLASS()
class SWGUI_API USWGHoloDetailCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetInfo(const FSWGExamineInfo& Info);

	/** One line under the name saying where the item is ("Equipped · chest1", "In your inventory"). */
	void SetStatus(const FText& Status);
	int64 GetObjectId() const { return ObjectId; }

	/** Restarts the unfold. */
	void PlayOpen();

	/** Marks it as pinned open, with a note on how to let it go. */
	void SetPinned(bool bPinned);

	/** Widest the card grows, slate units; long descriptions wrap. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float CardWidth = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloInventory")
	float UnfoldSeconds = 0.14f;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY()
	TObjectPtr<UBorder> Panel;

	UPROPERTY()
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UTextBlock> DescriptionText;

	UPROPERTY()
	TObjectPtr<UVerticalBox> AttributeBox;

	UPROPERTY()
	TObjectPtr<UTextBlock> FooterText;

	int64 ObjectId = 0;
	float UnfoldAlpha = 1.f;
};
