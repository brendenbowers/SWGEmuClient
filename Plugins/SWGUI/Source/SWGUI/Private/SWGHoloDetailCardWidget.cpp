#include "SWGHoloDetailCardWidget.h"
#include "SWGHoloAttributeLineWidget.h"
#include "SWGHoloStyle.h"
#include "SWGUISettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"

namespace
{
	UTextBlock* MakeText(UWidgetTree* Tree, int32 Size, const FLinearColor& Color, bool bBold = true)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetFont(SWGHoloStyle::Font(Size, bBold));
		Text->SetColorAndOpacity(FSlateColor(Color));
		return Text;
	}
}

USWGHoloDetailCardWidget* USWGHoloDetailCardWidget::Create(APlayerController* Owner)
{
	TSubclassOf<USWGHoloDetailCardWidget> Class = USWGUISettings::Get().HoloDetailCardClass.LoadSynchronous();
	return CreateWidget<USWGHoloDetailCardWidget>(Owner, Class ? Class.Get() : StaticClass());
}

void USWGHoloDetailCardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
		Width->SetMaxDesiredWidth(CardWidth);
		Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetPadding(FMargin(10.f, 8.f));
		Width->SetContent(Panel);

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Panel->SetContent(Column);
		NameText = MakeText(WidgetTree, 14, SWGHoloStyle::BrightText);
		NameText->SetAutoWrapText(true);
		Column->AddChildToVerticalBox(NameText);
		StatusText = MakeText(WidgetTree, 10, SWGHoloStyle::Text);
		Column->AddChildToVerticalBox(StatusText);
		DescriptionText = MakeText(WidgetTree, 11, SWGHoloStyle::DimText, false);
		DescriptionText->SetAutoWrapText(true);
		if (UVerticalBoxSlot* DescriptionSlot = Column->AddChildToVerticalBox(DescriptionText))
		{
			DescriptionSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		}
		UVerticalBox* Attributes = WidgetTree->ConstructWidget<UVerticalBox>();
		AttributeBox = Attributes;
		if (UVerticalBoxSlot* AttributeSlot = Column->AddChildToVerticalBox(Attributes))
		{
			AttributeSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		}
		FooterText = MakeText(WidgetTree, 10, SWGHoloStyle::DimText, false);
		if (UVerticalBoxSlot* FooterSlot = Column->AddChildToVerticalBox(FooterText))
		{
			FooterSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		}
		WidgetTree->RootWidget = Width;
	}
	if (bApplyHoloStyle)
	{
		if (Panel)
		{
			Panel->SetBrush(SWGHoloStyle::PanelBrush(true));
		}
		// The holo font is a system face, not an asset, so a Blueprint can't pick it.
		auto Style = [](UTextBlock* Text, int32 Size, bool bBold, const FLinearColor& Color)
		{
			if (Text)
			{
				Text->SetFont(SWGHoloStyle::Font(Size, bBold));
				Text->SetColorAndOpacity(FSlateColor(Color));
			}
		};
		Style(NameText, 14, true, SWGHoloStyle::BrightText);
		Style(StatusText, 10, true, SWGHoloStyle::Text);
		Style(DescriptionText, 11, false, SWGHoloStyle::DimText);
		Style(FooterText, 10, false, SWGHoloStyle::DimText);
	}
	if (StatusText)
	{
		StatusText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (FooterText)
	{
		FooterText->SetText(NSLOCTEXT("SWGEmu", "HoloCardPinned", "Pinned  •  click the name again to release"));
		FooterText->SetVisibility(ESlateVisibility::Collapsed);
	}
	// Details only; the pointer passes through to the names and the hologram.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void USWGHoloDetailCardWidget::SetInfo(const FSWGExamineInfo& Info)
{
	ObjectId = Info.ObjectId;
	if (NameText)
	{
		NameText->SetText(FText::FromString(Info.Name));
	}
	if (DescriptionText)
	{
		DescriptionText->SetText(FText::FromString(Info.Description));
		DescriptionText->SetVisibility(Info.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (!AttributeBox)
	{
		return;
	}
	AttributeBox->ClearChildren();
	FString Category;
	for (const FSWGExamineAttribute& Attribute : Info.Attributes)
	{
		// A heading wherever the group changes, as retail's examine window lays them out.
		if (!Attribute.Category.IsEmpty() && Attribute.Category != Category)
		{
			USWGHoloAttributeLineWidget* Heading = USWGHoloAttributeLineWidget::Create(GetOwningPlayer());
			Heading->SetHeading(FText::FromString(Attribute.Category));
			AttributeBox->AddChild(Heading);
		}
		Category = Attribute.Category;
		USWGHoloAttributeLineWidget* Line = USWGHoloAttributeLineWidget::Create(GetOwningPlayer());
		Line->SetAttribute(FText::FromString(Attribute.Label), FText::FromString(Attribute.Value), !Attribute.Category.IsEmpty());
		AttributeBox->AddChild(Line);
	}
}

void USWGHoloDetailCardWidget::SetStatus(const FText& Status)
{
	if (StatusText)
	{
		StatusText->SetText(Status);
		StatusText->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void USWGHoloDetailCardWidget::SetPinned(bool bPinned)
{
	if (FooterText)
	{
		FooterText->SetVisibility(bPinned ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
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
