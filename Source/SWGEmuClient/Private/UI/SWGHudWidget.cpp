#include "UI/SWGHudWidget.h"
#include "UI/SWGActionBarWidget.h"
#include "UI/SWGConditionWidget.h"

TWeakObjectPtr<USWGHudWidget> USWGHudWidget::ActiveHud;

USWGHudWidget* USWGHudWidget::GetActiveHud()
{
	return ActiveHud.Get();
}

void USWGHudWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ActiveHud = this;
}

void USWGHudWidget::NativeDestruct()
{
	if (ActiveHud.Get() == this)
	{
		ActiveHud.Reset();
	}

	Super::NativeDestruct();
}

bool USWGHudWidget::TriggerActionSlot(int32 SlotIndex)
{
	return ActionBar ? ActionBar->TriggerSlot(SlotIndex) : false;
}
