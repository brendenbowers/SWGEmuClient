#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "Components/TextBlock.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "SWGCommandQueueEntryWidget.generated.h"

/**
 * One line of the command queue: its place in the queue, the command, and how
 * long the server has held it. The queue widget creates these.
 */
UCLASS(Abstract)
class SWGUI_API USWGCommandQueueEntryWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Fills the line in. Position 0 is the command currently executing. */
	void SetEntry(const FSWGQueuedCommand& Entry, int32 Position);

	/** Refreshes just the elapsed readout, without rebuilding the line. */
	void SetElapsed(float ElapsedSeconds);

	/** Lets the Blueprint restyle the executing line differently from the waiting ones. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SWGEmu|CommandQueue")
	void OnEntrySet(const FSWGQueuedCommand& Entry, bool bIsExecuting);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PositionLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CommandLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ElapsedLabel;

private:
	/** Applies the cached text to the bound labels, once they exist. */
	void ApplyLabels();

	FText PendingPosition;
	FText PendingCommand;
	FText PendingElapsed;
};
