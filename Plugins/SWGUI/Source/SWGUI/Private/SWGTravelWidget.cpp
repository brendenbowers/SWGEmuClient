#include "SWGTravelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	const FLinearColor Cyan = FLinearColor::FromSRGBColor(FColor(0x69, 0xDD, 0xEE));
	const FLinearColor BrightCyan = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));
	const FLinearColor Green = FLinearColor::FromSRGBColor(FColor(0x37, 0xFD, 0x06));
	const FLinearColor Panel = FLinearColor(0.015f, 0.055f, 0.075f, 0.96f);
	constexpr float MapWidth = 740.f;
	constexpr float MapHeight = 380.f;

	FString PlanetIconPath(const FString& Planet)
	{
		static const TMap<FString, FString> Codes = {
			{ TEXT("corellia"), TEXT("corl") }, { TEXT("dantooine"), TEXT("dant") },
			{ TEXT("dathomir"), TEXT("dath") }, { TEXT("endor"), TEXT("endo") },
			{ TEXT("lok"), TEXT("lok") }, { TEXT("naboo"), TEXT("nboo") },
			{ TEXT("rori"), TEXT("rori") }, { TEXT("talus"), TEXT("talu") },
			{ TEXT("tatooine"), TEXT("tatt") }, { TEXT("yavin4"), TEXT("yavi") }
		};
		const FString* Code = Codes.Find(Planet);
		return Code ? FString::Printf(TEXT("texture/ui_planet_sel_%s.dds"), **Code) : FString();
	}

	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, const FLinearColor& Color, int32 Size = 15)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		Block->SetText(Text);
		Block->SetColorAndOpacity(FSlateColor(Color));
		FSlateFontInfo Font = Block->GetFont();
		Font.Size = Size;
		Block->SetFont(Font);
		return Block;
	}

	void AddVertical(UVerticalBox* Box, UWidget* Child, float Padding = 5.f, bool bFill = false)
	{
		if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Box->AddChild(Child)))
		{
			Slot->SetPadding(FMargin(0.f, Padding));
			Slot->SetHorizontalAlignment(HAlign_Fill);
			Slot->SetSize(FSlateChildSize(bFill ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic));
		}
	}
}

void USWGTravelWidget::BuildLayout()
{
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = RootCanvas;

	Frame = WidgetTree->ConstructWidget<USizeBox>();
	Frame->SetWidthOverride(820.f);
	Frame->SetHeightOverride(680.f);
	RootCanvas->AddChild(Frame);
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Frame->Slot))
	{
		CanvasSlot->SetPosition(FVector2D(160.f, 120.f));
		CanvasSlot->SetSize(FVector2D(820.f, 680.f));
	}

	UOverlay* FrameOverlay = WidgetTree->ConstructWidget<UOverlay>();
	Frame->AddChild(FrameOverlay);

	UBorder* Outer = WidgetTree->ConstructWidget<UBorder>();
	Outer->SetBrushColor(Cyan);
	Outer->SetPadding(FMargin(2.f));
	FrameOverlay->AddChild(Outer);

	UBorder* Inner = WidgetTree->ConstructWidget<UBorder>();
	Inner->SetBrushColor(Panel);
	Outer->SetContent(Inner);

	UVerticalBox* WindowBox = WidgetTree->ConstructWidget<UVerticalBox>();
	Inner->SetContent(WindowBox);

	Caption = WidgetTree->ConstructWidget<UBorder>();
	Caption->SetBrushColor(FLinearColor(0.02f, 0.18f, 0.23f, 1.f));
	Caption->SetPadding(FMargin(12.f, 6.f));
	AddVertical(WindowBox, Caption, 0.f);

	UHorizontalBox* CaptionRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Caption->SetContent(CaptionRow);
	TitleText = MakeText(WidgetTree, FText::GetEmpty(), BrightCyan, 17);
	if (UHorizontalBoxSlot* TitleSlot = Cast<UHorizontalBoxSlot>(CaptionRow->AddChild(TitleText)))
	{
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
	}
	CloseButton = WidgetTree->ConstructWidget<UButton>();
	CloseButton->AddChild(MakeText(WidgetTree, FText::FromString(TEXT("×")), BrightCyan, 18));
	CaptionRow->AddChild(CloseButton);

	Content = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UVerticalBoxSlot* ContentSlot = Cast<UVerticalBoxSlot>(WindowBox->AddChild(Content)))
	{
		ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ContentSlot->SetPadding(FMargin(18.f, 12.f, 18.f, 8.f));
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	UVerticalBox* Body = CastChecked<UVerticalBox>(Content);

	DepartureText = MakeText(WidgetTree, FText::GetEmpty(), BrightCyan, 15);
	AddVertical(Body, DepartureText, 4.f);

	UHorizontalBox* MapHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddVertical(Body, MapHeader, 4.f);
	MapTitleText = MakeText(WidgetTree, FText::GetEmpty(), Cyan, 14);
	if (UHorizontalBoxSlot* MapTitleSlot = Cast<UHorizontalBoxSlot>(MapHeader->AddChild(MapTitleText)))
	{
		MapTitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		MapTitleSlot->SetVerticalAlignment(VAlign_Center);
	}
	GalaxyButton = WidgetTree->ConstructWidget<UButton>();
	GalaxyButton->AddChild(MakeText(WidgetTree, NSLOCTEXT("SWGEmu", "TravelGalaxy", "GALAXY MAP"), BrightCyan, 13));
	if (UHorizontalBoxSlot* GalaxySlot = Cast<UHorizontalBoxSlot>(MapHeader->AddChild(GalaxyButton)))
	{
		GalaxySlot->SetPadding(FMargin(8.f, 0.f));
	}

	USizeBox* MapSize = WidgetTree->ConstructWidget<USizeBox>();
	MapSize->SetWidthOverride(MapWidth);
	MapSize->SetHeightOverride(MapHeight);
	AddVertical(Body, MapSize, 5.f, true);
	MapCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	MapSize->AddChild(MapCanvas);
	MapImage = WidgetTree->ConstructWidget<UImage>();

	UBorder* Divider = WidgetTree->ConstructWidget<UBorder>();
	Divider->SetBrushColor(FLinearColor(0.2f, 0.7f, 0.8f, 0.35f));
	Divider->SetDesiredSizeScale(FVector2D(1.f, 0.06f));
	AddVertical(Body, Divider, 8.f);

	UHorizontalBox* Options = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddVertical(Body, Options, 7.f);
	RoundTripCheck = WidgetTree->ConstructWidget<UCheckBox>();
	Options->AddChild(RoundTripCheck);
	if (UHorizontalBoxSlot* RoundTripSlot = Cast<UHorizontalBoxSlot>(Options->AddChild(MakeText(WidgetTree, NSLOCTEXT("SWGEmu", "TravelRoundTrip", "Round trip"), BrightCyan))))
	{
		RoundTripSlot->SetPadding(FMargin(7.f, 0.f));
		RoundTripSlot->SetVerticalAlignment(VAlign_Center);
	}
	FareText = MakeText(WidgetTree, FText::GetEmpty(), Green, 18);
	if (UHorizontalBoxSlot* FareSlot = Cast<UHorizontalBoxSlot>(Options->AddChild(FareText)))
	{
		FareSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		FareSlot->SetHorizontalAlignment(HAlign_Right);
		FareSlot->SetVerticalAlignment(VAlign_Center);
	}

	StatusText = MakeText(WidgetTree, FText::GetEmpty(), FLinearColor(0.75f, 0.85f, 0.9f), 13);
	AddVertical(Body, StatusText, 4.f);

	PurchaseButton = WidgetTree->ConstructWidget<UButton>();
	PurchaseButton->AddChild(MakeText(WidgetTree, NSLOCTEXT("SWGEmu", "TravelPurchase", "PURCHASE TICKET"), BrightCyan, 16));
	AddVertical(Body, PurchaseButton, 7.f);

	ControllerHelpText = MakeText(WidgetTree,
		NSLOCTEXT("SWGEmu", "TravelControllerHelp", "←/→ FIELD    ↑/↓ SELECT    A PURCHASE    X ROUND TRIP    B CLOSE"), Cyan, 12);
	ControllerHelpText->SetJustification(ETextJustify::Center);
	AddVertical(Body, ControllerHelpText, 5.f);

	ResizeGrip = MakeText(WidgetTree, FText::FromString(TEXT("◢")), Cyan, 12);
	if (UOverlaySlot* GripSlot = Cast<UOverlaySlot>(FrameOverlay->AddChild(ResizeGrip)))
	{
		GripSlot->SetHorizontalAlignment(HAlign_Right);
		GripSlot->SetVerticalAlignment(VAlign_Bottom);
		GripSlot->SetPadding(FMargin(4.f));
	}
}

void USWGTravelWidget::NativeOnInitialized()
{
	if (!RootCanvas)
	{
		BuildLayout();
	}
	Super::NativeOnInitialized();
}

void USWGTravelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetTitle(NSLOCTEXT("SWGEmu", "TravelTitle", "Ticket Purchase"));
	MinimumSize = FVector2D(720.f, 620.f);

	GalaxyButton->OnClicked.AddUniqueDynamic(this, &USWGTravelWidget::HandleGalaxyClicked);
	RoundTripCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USWGTravelWidget::HandleRoundTripChanged);
	PurchaseButton->OnClicked.AddUniqueDynamic(this, &USWGTravelWidget::HandlePurchaseClicked);

	Travel = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTravelSubsystem>() : nullptr;
	Tre = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
	if (Travel)
	{
		Travel->OnTravelDataChanged.AddDynamic(this, &USWGTravelWidget::HandleTravelDataChanged);
	}
	ApplyControllerMode();
	RefreshTravelData();
}

void USWGTravelWidget::NativeDestruct()
{
	if (Travel)
	{
		Travel->OnTravelDataChanged.RemoveDynamic(this, &USWGTravelWidget::HandleTravelDataChanged);
	}
	Super::NativeDestruct();
}

void USWGTravelWidget::SetControllerMode(bool bEnabled)
{
	bControllerMode = bEnabled;
	if (IsConstructed())
	{
		ApplyControllerMode();
	}
}

void USWGTravelWidget::ApplyControllerMode()
{
	ControllerHelpText->SetVisibility(bControllerMode ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ResizeGrip->SetVisibility(bControllerMode ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	SetWindowSize(bControllerMode ? FVector2D(940.f, 760.f) : FVector2D(820.f, 680.f));
	if (bControllerMode)
	{
		CenterOnScreen();
		SetFocus();
	}
	ControllerHelpText->SetText(bGalaxyView
		? NSLOCTEXT("SWGEmu", "TravelGalaxyControllerHelp", "D-PAD PLANET    A OPEN    B CLOSE")
		: NSLOCTEXT("SWGEmu", "TravelPlanetControllerHelp", "D-PAD STARPORT    A PURCHASE    X ROUND TRIP    B GALAXY"));
}

FText USWGTravelWidget::PlanetDisplayName(const FString& Planet)
{
	if (Planet.Equals(TEXT("yavin4"), ESearchCase::IgnoreCase))
	{
		return FText::FromString(TEXT("Yavin IV"));
	}
	FString Display = Planet;
	if (!Display.IsEmpty())
	{
		Display[0] = FChar::ToUpper(Display[0]);
	}
	return FText::FromString(Display);
}

FText USWGTravelWidget::LocationDisplayName(const FString& Location) const
{
	return FText::FromString(Tre ? Tre->ResolveStringId(Location) : Location);
}

void USWGTravelWidget::RefreshTravelData()
{
	if (!Travel)
	{
		return;
	}
	DepartureText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "TravelDeparting", "DEPARTING:  {0}  •  {1}"),
		PlanetDisplayName(Travel->GetDeparturePlanet()), LocationDisplayName(Travel->GetDepartureLocation())));
	VisiblePlanets = Travel->GetAvailablePlanets();
	if (SelectedPlanet.IsEmpty())
	{
		SelectedPlanet = Travel->GetDeparturePlanet();
	}
	SelectedPlanetIndex = VisiblePlanets.IndexOfByKey(SelectedPlanet);
	if (SelectedPlanetIndex == INDEX_NONE && !VisiblePlanets.IsEmpty())
	{
		SelectedPlanetIndex = 0;
		SelectedPlanet = VisiblePlanets[0];
	}
	if (bGalaxyView)
	{
		RebuildMap();
		RefreshFare();
		return;
	}
	ShowPlanetMap(SelectedPlanet);
}

void USWGTravelWidget::ShowGalaxyMap()
{
	bGalaxyView = true;
	SelectedDestinationIndex = INDEX_NONE;
	RebuildMap();
	RefreshFare();
	ApplyControllerMode();
}

void USWGTravelWidget::ShowPlanetMap(const FString& Planet)
	{
	const FString PreviousPlanet = SelectedPlanet;
	const FString PreviousLocation = VisibleDestinations.IsValidIndex(SelectedDestinationIndex)
		? VisibleDestinations[SelectedDestinationIndex].Location : FString();
	bGalaxyView = false;
	SelectedPlanet = Planet.ToLower();
	SelectedPlanetIndex = VisiblePlanets.IndexOfByKey(SelectedPlanet);
	VisibleDestinations = Travel ? Travel->GetDestinations(SelectedPlanet) : TArray<FSWGTravelDestination>();
	SelectedDestinationIndex = PreviousPlanet == SelectedPlanet && !PreviousLocation.IsEmpty()
		? VisibleDestinations.IndexOfByPredicate([&PreviousLocation](const FSWGTravelDestination& Destination)
		{
			return Destination.Location == PreviousLocation;
		})
		: INDEX_NONE;
	if (SelectedDestinationIndex == INDEX_NONE && !VisibleDestinations.IsEmpty())
	{
		SelectedDestinationIndex = 0;
	}
	RebuildMap();
	RefreshFare();
	ApplyControllerMode();
}

void USWGTravelWidget::RebuildMap()
	{
	if (!MapCanvas)
	{
		return;
	}
	MapCanvas->ClearChildren();
	MapClickForwarders.Reset();
	MapButtons.Reset();

	MapCanvas->AddChild(MapImage);
	if (UCanvasPanelSlot* ImageSlot = Cast<UCanvasPanelSlot>(MapImage->Slot))
	{
		ImageSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		ImageSlot->SetOffsets(FMargin(0.f));
	}

	if (bGalaxyView)
	{
		MapTitleText->SetText(NSLOCTEXT("SWGEmu", "TravelSelectPlanet", "SELECT A DESTINATION PLANET"));
		GalaxyButton->SetVisibility(ESlateVisibility::Collapsed);
		MapImage->SetVisibility(ESlateVisibility::Collapsed);
		for (int32 Index = 0; Index < VisiblePlanets.Num(); ++Index)
		{
			const FString Planet = VisiblePlanets[Index];
			UButton* Button = WidgetTree->ConstructWidget<UButton>();
			UVerticalBox* PlanetBox = WidgetTree->ConstructWidget<UVerticalBox>();
			const FString IconPath = PlanetIconPath(Planet);
			if (Tre && !IconPath.IsEmpty())
			{
				UImage* Icon = WidgetTree->ConstructWidget<UImage>();
				Icon->SetBrushFromTexture(Tre->GetOrLoadTexture(IconPath));
				USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>();
				IconSize->SetWidthOverride(72.f);
				IconSize->SetHeightOverride(72.f);
				IconSize->AddChild(Icon);
				PlanetBox->AddChild(IconSize);
			}
			PlanetBox->AddChild(MakeText(WidgetTree,
				FText::Format(FText::FromString(Index == SelectedPlanetIndex ? TEXT("◆ {0}") : TEXT("{0}")), PlanetDisplayName(Planet)),
				Index == SelectedPlanetIndex ? Green : BrightCyan, 13));
			Button->AddChild(PlanetBox);
			MapCanvas->AddChild(Button);
			if (UCanvasPanelSlot* MapSlot = Cast<UCanvasPanelSlot>(Button->Slot))
			{
				MapSlot->SetPosition(FVector2D(18.f + (Index % 5) * 144.f, 28.f + (Index / 5) * 168.f));
				MapSlot->SetSize(FVector2D(128.f, 140.f));
			}
			USWGTravelMapClickForwarder* Forwarder = NewObject<USWGTravelMapClickForwarder>(this);
			Forwarder->Action = [this, Planet]() { ShowPlanetMap(Planet); };
			Button->OnClicked.AddDynamic(Forwarder, &USWGTravelMapClickForwarder::HandleClicked);
			MapClickForwarders.Add(Forwarder);
			MapButtons.Add(Button);
		}
	}
	else
	{
		MapTitleText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "TravelSelectPort", "{0} — SELECT A STARPORT"), PlanetDisplayName(SelectedPlanet)));
		GalaxyButton->SetVisibility(ESlateVisibility::Visible);
		MapImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (Tre)
		{
			MapImage->SetBrushFromTexture(Tre->GetOrLoadTexture(FString::Printf(TEXT("texture/ui_map_%s.dds"), *SelectedPlanet)));
		}
		for (int32 Index = 0; Index < VisibleDestinations.Num(); ++Index)
		{
			const FSWGTravelDestination& Destination = VisibleDestinations[Index];
			UButton* Button = WidgetTree->ConstructWidget<UButton>();
			Button->AddChild(MakeText(WidgetTree,
				FText::Format(Index == SelectedDestinationIndex
					? NSLOCTEXT("SWGEmu", "TravelSelectedLocation", "◆ {0}")
					: NSLOCTEXT("SWGEmu", "TravelLocation", "• {0}"), LocationDisplayName(Destination.Location)),
				Index == SelectedDestinationIndex ? Green : BrightCyan, 12));
			MapCanvas->AddChild(Button);
			if (UCanvasPanelSlot* MapSlot = Cast<UCanvasPanelSlot>(Button->Slot))
			{
				const float X = FMath::Clamp((Destination.Position.X + 8192.f) / 16384.f, 0.f, 1.f) * MapWidth;
				const float Y = (1.f - FMath::Clamp((Destination.Position.Y + 8192.f) / 16384.f, 0.f, 1.f)) * MapHeight;
				MapSlot->SetPosition(FVector2D(FMath::Clamp(X - 95.f, 0.f, MapWidth - 190.f), FMath::Clamp(Y - 14.f, 0.f, MapHeight - 28.f)));
				MapSlot->SetSize(FVector2D(190.f, 28.f));
			}
			USWGTravelMapClickForwarder* Forwarder = NewObject<USWGTravelMapClickForwarder>(this);
			Forwarder->Action = [this, Index]() { SelectDestination(Index); };
			Button->OnClicked.AddDynamic(Forwarder, &USWGTravelMapClickForwarder::HandleClicked);
			MapClickForwarders.Add(Forwarder);
			MapButtons.Add(Button);
		}
	}
}

void USWGTravelWidget::RefreshFare()
{
	const FSWGTravelDestination* Destination = VisibleDestinations.IsValidIndex(SelectedDestinationIndex)
		? &VisibleDestinations[SelectedDestinationIndex] : nullptr;
	if (Destination && RoundTripCheck->IsChecked() && Travel && !Travel->CanBuyRoundTrip(*Destination))
	{
		RoundTripCheck->SetIsChecked(false);
		return;
	}
	const int32 Fare = Destination && Travel ? Travel->GetFare(Destination->Planet, RoundTripCheck->IsChecked()) : 0;
	FareText->SetText(Fare > 0
		? FText::Format(NSLOCTEXT("SWGEmu", "TravelFare", "{0} CREDITS"), FText::AsNumber(Fare))
		: FText::GetEmpty());
	PurchaseButton->SetIsEnabled(Destination && Fare > 0);
	StatusText->SetText(bGalaxyView
		? NSLOCTEXT("SWGEmu", "TravelChoosePlanet", "Choose a planet to view its arrival points.")
		: Destination
		? FText::Format(NSLOCTEXT("SWGEmu", "TravelCoordinates", "Arrival coordinates: {0}, {1}"),
			FText::AsNumber(FMath::RoundToInt(Destination->Position.X)), FText::AsNumber(FMath::RoundToInt(Destination->Position.Y)))
		: NSLOCTEXT("SWGEmu", "TravelLoading", "Loading available routes…"));
}

void USWGTravelWidget::HandleGalaxyClicked() { ShowGalaxyMap(); }
void USWGTravelWidget::HandleRoundTripChanged(bool bChecked) { RefreshFare(); }
void USWGTravelWidget::HandleTravelDataChanged() { RefreshTravelData(); }

void USWGTravelWidget::HandlePurchaseClicked()
{
	if (Travel && VisibleDestinations.IsValidIndex(SelectedDestinationIndex)
		&& Travel->PurchaseTicket(VisibleDestinations[SelectedDestinationIndex], RoundTripCheck->IsChecked()))
	{
		StatusText->SetText(NSLOCTEXT("SWGEmu", "TravelPurchaseSent", "Purchase request sent…"));
	}
}

void USWGTravelWidget::SelectPlanet(int32 Index)
{
	if (VisiblePlanets.IsValidIndex(Index))
	{
		SelectedPlanetIndex = Index;
		SelectedPlanet = VisiblePlanets[Index];
		RebuildMap();
	}
}

void USWGTravelWidget::SelectDestination(int32 Index)
{
	if (VisibleDestinations.IsValidIndex(Index))
	{
		SelectedDestinationIndex = Index;
		RebuildMap();
		RefreshFare();
	}
}

void USWGTravelWidget::CycleMapSelection(int32 Delta)
{
	if (bGalaxyView && !VisiblePlanets.IsEmpty())
	{
		SelectPlanet((FMath::Max(0, SelectedPlanetIndex) + Delta + VisiblePlanets.Num()) % VisiblePlanets.Num());
	}
	else if (!bGalaxyView && !VisibleDestinations.IsEmpty())
	{
		SelectDestination((FMath::Max(0, SelectedDestinationIndex) + Delta + VisibleDestinations.Num()) % VisibleDestinations.Num());
	}
}

FReply USWGTravelWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bControllerMode)
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left
		|| Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up)
	{
		CycleMapSelection(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right
		|| Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down)
	{
		CycleMapSelection(1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Right && !bGalaxyView)
	{
		ShowGalaxyMap();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Left)
	{
		RoundTripCheck->SetIsChecked(!RoundTripCheck->IsChecked());
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		if (bGalaxyView && VisiblePlanets.IsValidIndex(SelectedPlanetIndex))
		{
			ShowPlanetMap(VisiblePlanets[SelectedPlanetIndex]);
		}
		else
		{
			HandlePurchaseClicked();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
