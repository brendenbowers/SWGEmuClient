#include "SWGHoloLabelWidget.h"
#include "SWGHoloStyle.h"
#include "SWGUISettings.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"

USWGHoloLabelWidget* USWGHoloLabelWidget::Create(APlayerController* Owner)
{
	TSubclassOf<USWGHoloLabelWidget> Class = USWGUISettings::Get().HoloLabelClass.LoadSynchronous();
	if (!Class)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGHoloLabelWidget: HoloLabelClass is unset (Project Settings > SWG UI)."));
		return nullptr;
	}
	return CreateWidget<USWGHoloLabelWidget>(Owner, Class);
}

void USWGHoloLabelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// The holo font is a system face, not an asset, so a Blueprint can't pick it.
	if (bApplyHoloStyle)
	{
		Label->SetFont(SWGHoloStyle::Font(12));
	}
	SetVisibility(ESlateVisibility::Visible);
	ApplyStyle();
}

void USWGHoloLabelWidget::SetItem(int64 InObjectId, const FText& Text)
{
	ObjectId = InObjectId;
	Label->SetText(Text);
}

void USWGHoloLabelWidget::SetWrapWidth(float Width)
{
	Label->SetWrapTextAt(Width);
}

void USWGHoloLabelWidget::SetLit(bool bInLit)
{
	if (bLit == bInLit)
	{
		return;
	}
	bLit = bInLit;
	ApplyStyle();
}

void USWGHoloLabelWidget::ApplyStyle()
{
	const bool bBright = bHovered || bLit;
	if (bApplyHoloStyle)
	{
		Panel->SetBrush(SWGHoloStyle::PanelBrush(bBright));
		Label->SetColorAndOpacity(FSlateColor(bBright ? SWGHoloStyle::BrightText : SWGHoloStyle::Text));
	}
	OnBrightChanged(bBright);
}

void USWGHoloLabelWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	bHovered = true;
	ApplyStyle();
	OnHovered.ExecuteIfBound(ObjectId, true);
}

void USWGHoloLabelWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bHovered = false;
	ApplyStyle();
	OnHovered.ExecuteIfBound(ObjectId, false);
}

FReply USWGHoloLabelWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	OnPressed.ExecuteIfBound(ObjectId, InMouseEvent.GetEffectingButton(), InMouseEvent.GetScreenSpacePosition());
	return FReply::Handled();
}
