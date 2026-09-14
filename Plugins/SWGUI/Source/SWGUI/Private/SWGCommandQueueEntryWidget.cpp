#include "SWGCommandQueueEntryWidget.h"

void USWGCommandQueueEntryWidget::SetEntry(const FSWGQueuedCommand& Entry, int32 Position)
{
	PendingPosition = Position == 0
		? NSLOCTEXT("SWGEmu", "CommandQueueExecuting", ">")
		: FText::AsNumber(Position);
	PendingCommand = FText::FromString(Entry.CommandName);
	PendingElapsed = FText::FromString(FString::Printf(TEXT("%.1fs"), Entry.ElapsedSeconds));

	ApplyLabels();
	OnEntrySet(Entry, Position == 0);
}

void USWGCommandQueueEntryWidget::SetElapsed(float ElapsedSeconds)
{
	// Only this label is rewritten — the queue refreshes it every frame, and
	// re-setting the other two would invalidate them for nothing.
	PendingElapsed = FText::FromString(FString::Printf(TEXT("%.1fs"), ElapsedSeconds));

	if (ElapsedLabel)
	{
		ElapsedLabel->SetText(PendingElapsed);
	}
}

void USWGCommandQueueEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// The queue fills entries before they construct, so apply what it set.
	ApplyLabels();
}

void USWGCommandQueueEntryWidget::ApplyLabels()
{
	if (PositionLabel)
	{
		PositionLabel->SetText(PendingPosition);
	}
	if (CommandLabel)
	{
		CommandLabel->SetText(PendingCommand);
	}
	if (ElapsedLabel)
	{
		ElapsedLabel->SetText(PendingElapsed);
	}
}
