#include "SWGAudioOptionsWidget.h"
#include "SWGAudioSettings.h"
#include "SWGAudioSubsystem.h"
#include "SWGRetailStyle.h"
#include "SWGUISound.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"

namespace
{
	/** A slider row: the bus, its retail ui_opt_sound string key (null where retail has none) and the plain-English fallback. */
	struct FBusRow
	{
		ESWGAudioBus Bus;
		const TCHAR* RetailKey;
		const TCHAR* Fallback;
	};

	const FBusRow BusRows[] = {
		{ ESWGAudioBus::Master, TEXT("volume_master"), TEXT("Master Volume") },
		{ ESWGAudioBus::Music, TEXT("volume_background_music"), TEXT("Background Music Volume") },
		{ ESWGAudioBus::PlayerMusic, TEXT("volume_player_music"), TEXT("Player Music Volume") },
		{ ESWGAudioBus::Ambient, nullptr, TEXT("Ambient Volume") },
		{ ESWGAudioBus::Effects, TEXT("volume_effects"), TEXT("Effects Volume") },
		{ ESWGAudioBus::Combat, nullptr, TEXT("Combat Volume") },
		{ ESWGAudioBus::Creature, nullptr, TEXT("Creature and Voice Volume") },
		{ ESWGAudioBus::UI, TEXT("volume_user_interface"), TEXT("User Interface Volume") },
	};

	const FVector2D InitialSize(470.f, 430.f);
	constexpr float LabelWidth = 190.f;
	constexpr float PercentWidth = 44.f;
	const FLinearColor PanelFill(0.02f, 0.06f, 0.10f, 0.88f);
	const FLinearColor CaptionFill(0.04f, 0.20f, 0.26f, 0.9f);
	const FLinearColor Outline = SWGRetailStyle::HudTextNormal;

	UTextBlock* MakeText(UWidgetTree* Tree, const FString& Text, int32 PixelSize = 12)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		Block->SetText(FText::FromString(Text));
		Block->SetFont(SWGRetailStyle::Font(PixelSize));
		Block->SetColorAndOpacity(FSlateColor(SWGRetailStyle::HudTextNormal));
		return Block;
	}
}

void USWGAudioOptionsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = RootCanvas;

	Frame = WidgetTree->ConstructWidget<USizeBox>();
	Frame->SetWidthOverride(InitialSize.X);
	Frame->SetHeightOverride(InitialSize.Y);
	UCanvasPanelSlot* FrameSlot = RootCanvas->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.f, 0.f));
	FrameSlot->SetAlignment(FVector2D::ZeroVector);
	FrameSlot->SetAutoSize(true);

	UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>();
	Frame->AddChild(Layers);

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrush(FSlateRoundedBoxBrush(PanelFill, 6.f, Outline, 1.5f));
	Layers->AddChildToOverlay(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Column);

	Caption = WidgetTree->ConstructWidget<UBorder>();
	Caption->SetBrushColor(CaptionFill);
	Caption->SetPadding(FMargin(10.f, 4.f));
	Column->AddChildToVerticalBox(Caption);

	UHorizontalBox* CaptionRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Caption->SetContent(CaptionRow);
	TitleText = MakeText(WidgetTree, FString(), 13);
	UHorizontalBoxSlot* TitleSlot = CaptionRow->AddChildToHorizontalBox(TitleText);
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetVerticalAlignment(VAlign_Center);

	CloseButton = WidgetTree->ConstructWidget<UButton>();
	CloseButton->SetContent(MakeText(WidgetTree, TEXT("X")));
	CaptionRow->AddChildToHorizontalBox(CloseButton);

	Content = WidgetTree->ConstructWidget<UVerticalBox>();
	UVerticalBoxSlot* ContentSlot = Column->AddChildToVerticalBox(Content);
	ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ContentSlot->SetPadding(FMargin(14.f, 10.f));

	ResizeGrip = MakeText(WidgetTree, TEXT("::"), 10);
	ResizeGrip->SetVisibility(ESlateVisibility::HitTestInvisible);
	UOverlaySlot* GripSlot = Layers->AddChildToOverlay(ResizeGrip);
	GripSlot->SetHorizontalAlignment(HAlign_Right);
	GripSlot->SetVerticalAlignment(VAlign_Bottom);
	GripSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 2.f));

	MinimumSize = FVector2D(360.f, 300.f);
	SetTitle(FText::FromString(TEXT("Sound Options")));
}

void USWGAudioOptionsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		Audio = GameInstance->GetSubsystem<USWGAudioSubsystem>();
		Tre = GameInstance->GetSubsystem<USWGTreSubsystem>();
	}
	if (Sliders.IsEmpty())
	{
		BuildContent();
	}
	RefreshFromSettings();
}

void USWGAudioOptionsWidget::NativeDestruct()
{
	if (Audio)
	{
		Audio->SaveSettings();
	}
	Super::NativeDestruct();
}

FString USWGAudioOptionsWidget::Label(const TCHAR* RetailKey, const TCHAR* Fallback) const
{
	const FString Retail = (RetailKey && Tre) ? Tre->LookupString(TEXT("ui_opt_sound"), RetailKey) : FString();
	return Retail.IsEmpty() ? FString(Fallback) : Retail;
}

void USWGAudioOptionsWidget::BuildContent()
{
	UVerticalBox* ContentColumn = CastChecked<UVerticalBox>(Content);
	for (const FBusRow& Row : BusRows)
	{
		UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>();
		UVerticalBoxSlot* RowSlot = ContentColumn->AddChildToVerticalBox(RowBox);
		RowSlot->SetPadding(FMargin(0.f, 3.f));

		USizeBox* LabelBox = WidgetTree->ConstructWidget<USizeBox>();
		LabelBox->SetWidthOverride(LabelWidth);
		LabelBox->AddChild(MakeText(WidgetTree, Label(Row.RetailKey, Row.Fallback)));
		UHorizontalBoxSlot* LabelSlot = RowBox->AddChildToHorizontalBox(LabelBox);
		LabelSlot->SetVerticalAlignment(VAlign_Center);

		USlider* Slider = WidgetTree->ConstructWidget<USlider>();
		Slider->SetMinValue(0.f);
		Slider->SetMaxValue(1.f);
		Slider->SetStepSize(0.01f);
		Slider->SetSliderBarColor(FLinearColor(0.01f, 0.25f, 0.30f));
		Slider->SetSliderHandleColor(SWGRetailStyle::HudTextNormal);
		Slider->OnValueChanged.AddDynamic(this, &USWGAudioOptionsWidget::HandleSliderChanged);
		Slider->OnMouseCaptureEnd.AddDynamic(this, &USWGAudioOptionsWidget::HandleSliderCaptureEnd);
		UHorizontalBoxSlot* SliderSlot = RowBox->AddChildToHorizontalBox(Slider);
		SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SliderSlot->SetVerticalAlignment(VAlign_Center);
		Sliders.Add(Slider);

		USizeBox* PercentBox = WidgetTree->ConstructWidget<USizeBox>();
		PercentBox->SetWidthOverride(PercentWidth);
		UTextBlock* Percent = MakeText(WidgetTree, TEXT("100%"));
		Percent->SetJustification(ETextJustify::Right);
		PercentBox->AddChild(Percent);
		UHorizontalBoxSlot* PercentSlot = RowBox->AddChildToHorizontalBox(PercentBox);
		PercentSlot->SetVerticalAlignment(VAlign_Center);
		PercentLabels.Add(Percent);
	}

	UHorizontalBox* MuteRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBoxSlot* MuteRowSlot = ContentColumn->AddChildToVerticalBox(MuteRow);
	MuteRowSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 3.f));
	MuteCheck = WidgetTree->ConstructWidget<UCheckBox>();
	MuteCheck->OnCheckStateChanged.AddDynamic(this, &USWGAudioOptionsWidget::HandleMuteChanged);
	UHorizontalBoxSlot* CheckSlot = MuteRow->AddChildToHorizontalBox(MuteCheck);
	CheckSlot->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* MuteLabelSlot = MuteRow->AddChildToHorizontalBox(MakeText(WidgetTree, TEXT("Mute all sound")));
	MuteLabelSlot->SetVerticalAlignment(VAlign_Center);
	MuteLabelSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));

	// Pushes the buttons to the bottom of the window.
	UVerticalBoxSlot* FillerSlot = ContentColumn->AddChildToVerticalBox(WidgetTree->ConstructWidget<USizeBox>());
	FillerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	ContentColumn->AddChildToVerticalBox(ButtonRow);

	UButton* ResetButton = WidgetTree->ConstructWidget<UButton>();
	ResetButton->SetContent(MakeText(WidgetTree, TEXT("Reset to defaults")));
	ResetButton->OnClicked.AddDynamic(this, &USWGAudioOptionsWidget::HandleResetClicked);
	ButtonTextTints.Add(SWGRetailStyle::ApplyHudButton(ResetButton, Tre));
	UHorizontalBoxSlot* ResetSlot = ButtonRow->AddChildToHorizontalBox(ResetButton);
	ResetSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

	UButton* DoneButton = WidgetTree->ConstructWidget<UButton>();
	DoneButton->SetContent(MakeText(WidgetTree, TEXT("Close")));
	DoneButton->OnClicked.AddDynamic(this, &USWGAudioOptionsWidget::HandleCloseButtonClicked);
	ButtonTextTints.Add(SWGRetailStyle::ApplyHudButton(DoneButton, Tre));
	ButtonRow->AddChildToHorizontalBox(DoneButton);
}

void USWGAudioOptionsWidget::RefreshFromSettings()
{
	if (!Audio)
	{
		return;
	}
	for (int32 RowIndex = 0; RowIndex < Sliders.Num(); ++RowIndex)
	{
		const float Volume = Audio->GetBusVolume(BusRows[RowIndex].Bus);
		Sliders[RowIndex]->SetValue(Volume);
		PercentLabels[RowIndex]->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Volume * 100.f))));
	}
	if (MuteCheck)
	{
		MuteCheck->SetIsChecked(Audio->IsMuted());
	}
}

void USWGAudioOptionsWidget::HandleSliderChanged(float Value)
{
	// One handler serves every slider; the one that moved is the one that disagrees with the mix.
	if (!Audio)
	{
		return;
	}
	for (int32 RowIndex = 0; RowIndex < Sliders.Num(); ++RowIndex)
	{
		const float SliderValue = Sliders[RowIndex]->GetValue();
		if (!FMath::IsNearlyEqual(SliderValue, Audio->GetBusVolume(BusRows[RowIndex].Bus), 0.0001f))
		{
			Audio->SetBusVolume(BusRows[RowIndex].Bus, SliderValue);
			PercentLabels[RowIndex]->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(SliderValue * 100.f))));
		}
	}
}

void USWGAudioOptionsWidget::HandleSliderCaptureEnd()
{
	// A click on release lets the player hear the new level on the UI bus.
	SWGUISound::PlayClick(this);
}

void USWGAudioOptionsWidget::HandleMuteChanged(bool bChecked)
{
	if (Audio)
	{
		Audio->SetMuted(bChecked);
	}
}

void USWGAudioOptionsWidget::HandleResetClicked()
{
	if (!Audio)
	{
		return;
	}
	USWGAudioSettings::Get()->ResetToDefaults();
	Audio->ApplySettings();
	Audio->SaveSettings();
	RefreshFromSettings();
}

void USWGAudioOptionsWidget::HandleCloseButtonClicked()
{
	Close();
}
