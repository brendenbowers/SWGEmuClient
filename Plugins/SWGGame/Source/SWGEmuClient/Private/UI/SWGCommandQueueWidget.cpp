#include "UI/SWGCommandQueueWidget.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Engine/GameInstance.h"

USWGCommandSubsystem* USWGCommandQueueWidget::GetCommands() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<USWGCommandSubsystem>() : nullptr;
}

void USWGCommandQueueWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (USWGCommandSubsystem* Commands = GetCommands())
	{
		Commands->OnCommandQueueChanged.AddDynamic(this, &USWGCommandQueueWidget::HandleQueueChanged);
	}

	RefreshQueue();
}

void USWGCommandQueueWidget::NativeDestruct()
{
	if (USWGCommandSubsystem* Commands = GetCommands())
	{
		Commands->OnCommandQueueChanged.RemoveDynamic(this, &USWGCommandQueueWidget::HandleQueueChanged);
	}

	Super::NativeDestruct();
}

void USWGCommandQueueWidget::HandleQueueChanged()
{
	RefreshQueue();
}

void USWGCommandQueueWidget::RefreshQueue()
{
	const USWGCommandSubsystem* Commands = GetCommands();
	const int32 QueueLength = Commands ? Commands->GetQueueLength() : 0;

	if (bHideWhenEmpty)
	{
		SetVisibility(QueueLength > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (TitleLabel)
	{
		TitleLabel->SetText(FText::Format(NSLOCTEXT("SWGEmu", "CommandQueueTitle", "Queue ({0})"), FText::AsNumber(QueueLength)));
	}

	if (!EntryBox || !EntryWidgetClass)
	{
		return;
	}

	const int32 VisibleCount = FMath::Min(QueueLength, FMath::Max(MaxVisibleEntries, 0));

	// Grow the line pool as needed; spare lines are hidden rather than
	// destroyed, since the queue churns every swing.
	while (EntryWidgets.Num() < VisibleCount)
	{
		USWGCommandQueueEntryWidget* Entry = CreateWidget<USWGCommandQueueEntryWidget>(this, EntryWidgetClass);
		if (!Entry)
		{
			break;
		}

		EntryWidgets.Add(Entry);
		EntryBox->AddChild(Entry);
	}

	for (int32 Index = 0; Index < EntryWidgets.Num(); ++Index)
	{
		USWGCommandQueueEntryWidget* Entry = EntryWidgets[Index];
		if (!Entry)
		{
			continue;
		}

		if (Index < VisibleCount)
		{
			Entry->SetVisibility(ESlateVisibility::HitTestInvisible);
			Entry->SetEntry(Commands->GetQueuedCommands()[Index], Index);
		}
		else
		{
			Entry->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void USWGCommandQueueWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const USWGCommandSubsystem* Commands = GetCommands();
	if (!Commands)
	{
		return;
	}

	// Only the elapsed readouts move between queue changes, so refresh those
	// alone — a full rebuild every frame would recreate nothing useful.
	const TArray<FSWGQueuedCommand>& Queue = Commands->GetQueuedCommands();
	for (int32 Index = 0; Index < EntryWidgets.Num() && Index < Queue.Num(); ++Index)
	{
		if (USWGCommandQueueEntryWidget* Entry = EntryWidgets[Index])
		{
			Entry->SetElapsed(Queue[Index].ElapsedSeconds);
		}
	}
}
