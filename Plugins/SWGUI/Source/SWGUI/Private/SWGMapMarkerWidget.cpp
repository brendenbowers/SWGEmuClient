#include "SWGMapMarkerWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "SWGRetailStyle.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	const FName PlayerStyle(TEXT("Player"));
	const FLinearColor PlayerArrowColor = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));
}

bool FSWGMapMarker::LooksLike(const FSWGMapMarker& Other) const
{
	return Id == Other.Id && Label.EqualTo(Other.Label) && Style == Other.Style && LabelColor == Other.LabelColor
		&& bSelected == Other.bSelected && bCustomPinColor == Other.bCustomPinColor && PinColor == Other.PinColor
		&& bHasHeading == Other.bHasHeading;
}

void USWGMapMarkerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		// Retail's travel map: the city pin with its name above it, and an
		// arrow in the same spot for a heading marker.
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		WidgetTree->RootWidget = Column;
		Column->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		LabelText = WidgetTree->ConstructWidget<UTextBlock>();
		LabelText->SetShadowOffset(FVector2D(1.f, 1.f));
		LabelText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
		LabelText->SetJustification(ETextJustify::Center);
		LabelText->SetVisibility(ESlateVisibility::HitTestInvisible);
		Column->AddChildToVerticalBox(LabelText)->SetHorizontalAlignment(HAlign_Center);

		UOverlay* PinArea = WidgetTree->ConstructWidget<UOverlay>();
		Column->AddChildToVerticalBox(PinArea)->SetHorizontalAlignment(HAlign_Center);

		USizeBox* PinSize = WidgetTree->ConstructWidget<USizeBox>();
		PinSize->SetWidthOverride(SWGRetailStyle::PinSize.X);
		PinSize->SetHeightOverride(SWGRetailStyle::PinSize.Y);
		PinButton = WidgetTree->ConstructWidget<UButton>();
		PinSize->AddChild(PinButton);
		PinArea->AddChildToOverlay(PinSize);

		UTextBlock* Arrow = WidgetTree->ConstructWidget<UTextBlock>();
		Arrow->SetText(FText::FromString(TEXT("▲")));
		Arrow->SetFont(SWGRetailStyle::Font(16));
		Arrow->SetColorAndOpacity(FSlateColor(PlayerArrowColor));
		Arrow->SetShadowOffset(FVector2D(1.f, 1.f));
		Arrow->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
		Arrow->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Arrow->SetVisibility(ESlateVisibility::Collapsed);
		if (UOverlaySlot* ArrowSlot = PinArea->AddChildToOverlay(Arrow))
		{
			ArrowSlot->SetHorizontalAlignment(HAlign_Center);
			ArrowSlot->SetVerticalAlignment(VAlign_Center);
		}
		HeadingIndicator = Arrow;
	}
	if (PinButton)
	{
		PinButton->OnClicked.AddUniqueDynamic(this, &USWGMapMarkerWidget::HandlePinClicked);
	}
}

void USWGMapMarkerWidget::ApplyMarker_Implementation(const FSWGMapMarker& InMarker)
{
	const bool bPlayer = InMarker.Style == PlayerStyle;
	if (LabelText)
	{
		LabelText->SetText(InMarker.Label);
		LabelText->SetColorAndOpacity(FSlateColor(InMarker.LabelColor));
		LabelText->SetFont(SWGRetailStyle::Font(InMarker.bSelected ? 13 : 12));
		LabelText->SetVisibility(InMarker.Label.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (PinButton)
	{
		USWGTreSubsystem* Tre = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
		const FLinearColor IdleColor = InMarker.bSelected ? SWGRetailStyle::PinActivated
			: InMarker.bCustomPinColor ? InMarker.PinColor : SWGRetailStyle::PinDefault;
		PinButton->SetStyle(SWGRetailStyle::MakePinStyle(Tre, IdleColor));
		PinButton->SetVisibility(bPlayer ? ESlateVisibility::Hidden : ESlateVisibility::Visible);
	}
	if (HeadingIndicator)
	{
		HeadingIndicator->SetVisibility(InMarker.bHasHeading ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	// An unlabelled heading arrow is centred on its point; a pin stands on it.
	Anchor = bPlayer && InMarker.Label.IsEmpty() ? FVector2D(0.5f, 0.5f) : FVector2D(0.5f, 1.f);
}

void USWGMapMarkerWidget::ApplyScreenHeading_Implementation(float ScreenDegrees)
{
	if (HeadingIndicator)
	{
		HeadingIndicator->SetRenderTransformAngle(ScreenDegrees);
	}
}

void USWGMapMarkerWidget::NotifyClicked()
{
	OnClicked.Broadcast(this);
}

void USWGMapMarkerWidget::HandlePinClicked()
{
	NotifyClicked();
}
