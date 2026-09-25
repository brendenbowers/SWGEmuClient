#include "SWGHoloDetailCardWidget.h"
#include "SWGHoloAttributeLineWidget.h"
#include "SWGHoloStyle.h"
#include "SWGUISettings.h"
#include "Components/Border.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"

USWGHoloDetailCardWidget* USWGHoloDetailCardWidget::Create(APlayerController* Owner)
{
	TSubclassOf<USWGHoloDetailCardWidget> Class = USWGUISettings::Get().HoloDetailCardClass.LoadSynchronous();
	if (!Class)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGHoloDetailCardWidget: HoloDetailCardClass is unset (Project Settings > SWG UI)."));
		return nullptr;
	}
	return CreateWidget<USWGHoloDetailCardWidget>(Owner, Class);
}

void USWGHoloDetailCardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (bApplyHoloStyle)
	{
		Panel->SetBrush(SWGHoloStyle::PanelBrush(true));
		// The holo font is a system face, not an asset, so a Blueprint can't pick it.
		auto Style = [](UTextBlock* Text, int32 Size, bool bBold, const FLinearColor& Color)
		{
			Text->SetFont(SWGHoloStyle::Font(Size, bBold));
			Text->SetColorAndOpacity(FSlateColor(Color));
		};
		Style(NameText, 14, true, SWGHoloStyle::BrightText);
		Style(StatusText, 10, true, SWGHoloStyle::Text);
		Style(DescriptionText, 11, false, SWGHoloStyle::DimText);
		Style(FooterText, 10, false, SWGHoloStyle::DimText);
	}
	StatusText->SetVisibility(ESlateVisibility::Collapsed);
	FooterText->SetVisibility(ESlateVisibility::Collapsed);
	// Details only; the pointer passes through to the names and the hologram.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void USWGHoloDetailCardWidget::SetInfo(const FSWGExamineInfo& Info)
{
	ObjectId = Info.ObjectId;
	NameText->SetText(FText::FromString(Info.Name));
	DescriptionText->SetText(FText::FromString(Info.Description));
	DescriptionText->SetVisibility(Info.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	AttributeBox->ClearChildren();
	FString Category;
	for (const FSWGExamineAttribute& Attribute : Info.Attributes)
	{
		// A heading wherever the group changes, as retail's examine window lays them out.
		if (!Attribute.Category.IsEmpty() && Attribute.Category != Category)
		{
			if (USWGHoloAttributeLineWidget* Heading = USWGHoloAttributeLineWidget::Create(GetOwningPlayer()))
			{
				Heading->SetHeading(FText::FromString(Attribute.Category));
				AttributeBox->AddChild(Heading);
			}
		}
		Category = Attribute.Category;
		if (USWGHoloAttributeLineWidget* Line = USWGHoloAttributeLineWidget::Create(GetOwningPlayer()))
		{
			Line->SetAttribute(FText::FromString(Attribute.Label), FText::FromString(Attribute.Value), !Attribute.Category.IsEmpty());
			AttributeBox->AddChild(Line);
		}
	}
}

void USWGHoloDetailCardWidget::SetStatus(const FText& Status)
{
	StatusText->SetText(Status);
	StatusText->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void USWGHoloDetailCardWidget::SetPinned(bool bPinned)
{
	FooterText->SetVisibility(bPinned ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void USWGHoloDetailCardWidget::PlayOpen()
{
	UnfoldAlpha = 0.f;
	SetRenderTransformPivot(FVector2D(0.5f, 0.f));
	SetRenderScale(FVector2D(1.f, 0.f));
	SetRenderOpacity(0.f);
}

void USWGHoloDetailCardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (UnfoldAlpha >= 1.f)
	{
		return;
	}
	UnfoldAlpha = FMath::Min(1.f, UnfoldAlpha + InDeltaTime / FMath::Max(UnfoldSeconds, 0.01f));
	const float Eased = FMath::InterpEaseOut(0.f, 1.f, UnfoldAlpha, 2.f);
	SetRenderScale(FVector2D(1.f, Eased));
	SetRenderOpacity(Eased);
}
