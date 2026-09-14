#include "SWGActionSlotWidget.h"
#include "SWGActionBarWidget.h"

void USWGActionSlotWidget::InitialiseSlot(USWGActionBarWidget* InOwningBar, int32 InSlotIndex, const FText& InKeyLabel)
{
	OwningBar = InOwningBar;
	SlotIndex = InSlotIndex;
	PendingKeyLabel = InKeyLabel;

	if (KeyLabel)
	{
		KeyLabel->SetText(PendingKeyLabel);
	}
}

void USWGActionSlotWidget::SetCommandLabel(const FText& InLabel)
{
	PendingCommandLabel = InLabel;

	if (CommandLabel)
	{
		CommandLabel->SetText(PendingCommandLabel);
	}
}

void USWGActionSlotWidget::SetCommandIcon(const FSlateBrush* Brush)
{
	bHasPendingIcon = Brush != nullptr;
	PendingIconBrush = Brush ? *Brush : FSlateBrush();
	ApplyIcon();
}

void USWGActionSlotWidget::SetFrame(const FSlateBrush* Brush)
{
	bHasPendingFrame = Brush != nullptr;
	PendingFrameBrush = Brush ? *Brush : FSlateBrush();
	ApplyFrame();
}

void USWGActionSlotWidget::ApplyIcon()
{
	if (CommandIcon)
	{
		CommandIcon->SetBrush(PendingIconBrush);
		CommandIcon->SetColorAndOpacity(IconTint);
		CommandIcon->SetVisibility(bHasPendingIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void USWGActionSlotWidget::ApplyFrame()
{
	// No retail frame means the Blueprint's own brush stays; only the tint is ours.
	if (SlotFrame && bHasPendingFrame)
	{
		SlotFrame->SetBrush(PendingFrameBrush);
		SlotFrame->SetColorAndOpacity(FrameTint);
	}
}

void USWGActionSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SlotButton)
	{
		SlotButton->OnClicked.AddDynamic(this, &USWGActionSlotWidget::HandleClicked);
	}

	// The bar sets these before the slot is constructed, so apply them now that
	// the bound widgets exist.
	if (KeyLabel)
	{
		KeyLabel->SetText(PendingKeyLabel);
	}
	if (CommandLabel)
	{
		CommandLabel->SetText(PendingCommandLabel);
	}
	ApplyIcon();
	ApplyFrame();
}

void USWGActionSlotWidget::HandleClicked()
{
	if (USWGActionBarWidget* Bar = OwningBar.Get())
	{
		Bar->TriggerSlot(SlotIndex);
	}
}
