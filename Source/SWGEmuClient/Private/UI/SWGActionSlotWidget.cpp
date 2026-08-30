#include "UI/SWGActionSlotWidget.h"
#include "UI/SWGActionBarWidget.h"

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
}

void USWGActionSlotWidget::HandleClicked()
{
	if (USWGActionBarWidget* Bar = OwningBar.Get())
	{
		Bar->TriggerSlot(SlotIndex);
	}
}
