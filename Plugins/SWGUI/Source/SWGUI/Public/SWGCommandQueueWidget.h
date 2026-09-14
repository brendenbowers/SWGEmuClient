#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "SWGCommandQueueEntryWidget.h"
#include "SWGCommandQueueWidget.generated.h"

class USWGCommandSubsystem;

/**
 * Lists the commands the server has not answered yet, oldest first — the
 * client's view of its command queue.
 *
 * Rebuilt from USWGCommandSubsystem whenever the queue changes; the elapsed
 * readouts are refreshed on tick in between.
 */
UCLASS(Abstract)
class SWGUI_API USWGCommandQueueWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Re-reads the queue and rebuilds the lines. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|CommandQueue")
	void RefreshQueue();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Hide the whole panel while nothing is queued, as SWG does. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|CommandQueue")
	bool bHideWhenEmpty = true;

	/** How many lines to show; anything past this is left off. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|CommandQueue")
	int32 MaxVisibleEntries = 8;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|CommandQueue")
	TSubclassOf<USWGCommandQueueEntryWidget> EntryWidgetClass;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> EntryBox;

	/** Optional header, e.g. "Queue (3)". */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleLabel;

	UPROPERTY()
	TArray<TObjectPtr<USWGCommandQueueEntryWidget>> EntryWidgets;

private:
	USWGCommandSubsystem* GetCommands() const;

	UFUNCTION()
	void HandleQueueChanged();
};
