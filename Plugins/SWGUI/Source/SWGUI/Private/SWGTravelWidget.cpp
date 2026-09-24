#include "SWGTravelWidget.h"
#include "SWGPlanetMapWidget.h"
#include "SWGRetailStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameInstance.h"
#include "Objects/Player/SWGPlayer.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGWaypointSubsystem.h"
#include "TRE/SWGCrc32.h"

namespace
{
	const FLinearColor BrightCyan = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));
	const FLinearColor Green = FLinearColor::FromSRGBColor(FColor(0x37, 0xFD, 0x06));
	const FName TravelLayer(TEXT("Travel"));
	const FName WaypointLayer(TEXT("Waypoints"));
	/** Fly-to eye distance when a travel point or waypoint is picked, in metres. */
	constexpr float MarkerViewDistance = 1400.f;

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
}

void USWGTravelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetTitle(NSLOCTEXT("SWGEmu", "TravelTitle", "Ticket Purchase"));
	MinimumSize = FVector2D(720.f, 620.f);

	GalaxyButton->OnClicked.AddUniqueDynamic(this, &USWGTravelWidget::HandleGalaxyClicked);
	ZoomInButton->OnClicked.AddUniqueDynamic(this, &USWGTravelWidget::HandleZoomInClicked);
	ZoomOutButton->OnClicked.AddUniqueDynamic(this, &USWGTravelWidget::HandleZoomOutClicked);
	RoundTripCheck->OnCheckStateChanged.AddUniqueDynamic(this, &USWGTravelWidget::HandleRoundTripChanged);
	PurchaseButton->OnClicked.AddUniqueDynamic(this, &USWGTravelWidget::HandlePurchaseClicked);

	UGameInstance* GameInstance = GetGameInstance();
	Travel = GameInstance ? GameInstance->GetSubsystem<USWGTravelSubsystem>() : nullptr;
	Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	Waypoints = GameInstance ? GameInstance->GetSubsystem<USWGWaypointSubsystem>() : nullptr;
	if (Travel)
	{
		Travel->OnTravelDataChanged.AddDynamic(this, &USWGTravelWidget::HandleTravelDataChanged);
	}
	if (Waypoints)
	{
		Waypoints->OnWaypointListChanged.AddDynamic(this, &USWGTravelWidget::RefreshWaypointMarkers);
	}

	if (MapImage)
	{
		MapImage->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ButtonTextTints.IsEmpty())
	{
		for (UButton* Button : { GalaxyButton.Get(), PurchaseButton.Get(), ZoomInButton.Get(), ZoomOutButton.Get() })
		{
			if (UObject* TextTint = SWGRetailStyle::ApplyHudButton(Button, Tre))
			{
				ButtonTextTints.Add(TextTint);
			}
		}
	}
	if (!MapView)
	{
		MapView = CreateWidget<USWGPlanetMapWidget>(this, USWGPlanetMapWidget::StaticClass());
		MapView->OnMarkerClicked.AddUObject(this, &USWGTravelWidget::HandleMapMarkerClicked);
		MapView->OnPressed.AddWeakLambda(this, [this]() { OnPressed.Broadcast(this); });
		MapCanvas->AddChild(MapView);
		if (UCanvasPanelSlot* ViewSlot = Cast<UCanvasPanelSlot>(MapView->Slot))
		{
			ViewSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			ViewSlot->SetOffsets(FMargin(0.f));
		}
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
	if (Waypoints)
	{
		Waypoints->OnWaypointListChanged.RemoveDynamic(this, &USWGTravelWidget::RefreshWaypointMarkers);
	}
	Super::NativeDestruct();
}

void USWGTravelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	int32 Cash = 0;
	int32 Bank = 0;
	if (GetPlayerCredits(Cash, Bank) && (Cash != ShownCash || Bank != ShownBank))
	{
		RefreshFare();
	}
	if (!bGalaxyView && MapView && MapView->IsTerrainReady() != bTerrainWasReady)
	{
		bTerrainWasReady = MapView->IsTerrainReady();
		RefreshFare();
	}
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
		: NSLOCTEXT("SWGEmu", "TravelPlanetControllerHelp", "D-PAD STARPORT    A PURCHASE    X ROUND TRIP    LB/RB ZOOM    RS ORBIT    B GALAXY"));
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
	const bool bWasGalaxy = bGalaxyView;
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
	if (MapView)
	{
		if (bWasGalaxy)
		{
			// Coming back from the galaxy always opens on the overview.
			MapView->ResetView();
		}
		TArray<FVector2D> FocusPoints;
		for (const FSWGTravelDestination& Destination : VisibleDestinations)
		{
			FocusPoints.Add(Destination.Position);
		}
		MapView->ShowPlanet(SelectedPlanet, FocusPoints);
	}
	RebuildMap();
	RefreshFare();
	ApplyControllerMode();
}

void USWGTravelWidget::RebuildMap()
{
	for (UButton* Button : PlanetButtons)
	{
		Button->RemoveFromParent();
	}
	PlanetButtons.Reset();
	PlanetClickForwarders.Reset();

	if (!bGalaxyView)
	{
		MapTitleText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "TravelSelectPort", "{0} — SELECT A STARPORT"), PlanetDisplayName(SelectedPlanet)));
		GalaxyButton->SetVisibility(ESlateVisibility::Visible);
		ZoomInButton->SetVisibility(ESlateVisibility::Visible);
		ZoomOutButton->SetVisibility(ESlateVisibility::Visible);
		if (MapView)
		{
			MapView->SetVisibility(ESlateVisibility::Visible);
		}
		RefreshTravelMarkers();
		RefreshWaypointMarkers();
		return;
	}

	MapTitleText->SetText(NSLOCTEXT("SWGEmu", "TravelSelectPlanet", "SELECT A DESTINATION PLANET"));
	GalaxyButton->SetVisibility(ESlateVisibility::Collapsed);
	ZoomInButton->SetVisibility(ESlateVisibility::Collapsed);
	ZoomOutButton->SetVisibility(ESlateVisibility::Collapsed);
	if (MapView)
	{
		MapView->SetVisibility(ESlateVisibility::Collapsed);
	}
	for (int32 Index = 0; Index < VisiblePlanets.Num(); ++Index)
	{
		const FString Planet = VisiblePlanets[Index];
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		Button->SetStyle(SWGRetailStyle::MakeHudButtonStyle(Tre, /*bTransparentIdle=*/true));
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
		USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
		Forwarder->Action = [this, Planet]() { ShowPlanetMap(Planet); };
		Button->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
		PlanetClickForwarders.Add(Forwarder);
		PlanetButtons.Add(Button);
	}
}

void USWGTravelWidget::RefreshTravelMarkers()
{
	if (!MapView)
	{
		return;
	}
	TArray<FSWGMapMarker> Markers;
	for (int32 Index = 0; Index < VisibleDestinations.Num(); ++Index)
	{
		const bool bSelected = Index == SelectedDestinationIndex;
		FSWGMapMarker& Marker = Markers.AddDefaulted_GetRef();
		Marker.Id = FName(*FString::FromInt(Index));
		Marker.Position = VisibleDestinations[Index].Position;
		Marker.Label = LocationDisplayName(VisibleDestinations[Index].Location);
		Marker.bSelected = bSelected;
	}
	MapView->SetMarkers(TravelLayer, Markers);
}

void USWGTravelWidget::RefreshWaypointMarkers()
{
	if (!MapView)
	{
		return;
	}
	WaypointPositions.Reset();
	TArray<FSWGMapMarker> Markers;
	// Core3's waypoint planet CRC is the zone name's String::hashCode.
	const int32 PlanetCrc = static_cast<int32>(FSWGCrc32::HashString(SelectedPlanet));
	for (const FSWGWaypointEntry& Waypoint : Waypoints ? Waypoints->GetWaypoints() : TArray<FSWGWaypointEntry>())
	{
		// A cell waypoint's position is local to its room, not placeable on the map.
		if (Waypoint.PlanetCRC != PlanetCrc || Waypoint.CellId != 0)
		{
			continue;
		}
		FSWGMapMarker& Marker = Markers.AddDefaulted_GetRef();
		Marker.Id = FName(*LexToString(Waypoint.WaypointObjectId));
		Marker.Position = FVector2D(Waypoint.RawPosition.X, Waypoint.RawPosition.Y);
		Marker.Label = Waypoint.Name;
		Marker.bCustomPinColor = true;
		Marker.PinColor = USWGWaypointSubsystem::GetWaypointColor(Waypoint.Color);
		Marker.PinColor.A = Waypoint.bActive ? 1.f : 0.55f;
		Marker.LabelColor = Marker.PinColor;
		WaypointPositions.Add(Marker.Id, Marker.Position);
	}
	MapView->SetMarkers(WaypointLayer, Markers);
}

void USWGTravelWidget::HandleMapMarkerClicked(FName Layer, FName MarkerId)
{
	if (Layer == TravelLayer)
	{
		SelectDestination(FCString::Atoi(*MarkerId.ToString()));
	}
	else if (const FVector2D* Position = WaypointPositions.Find(MarkerId))
	{
		MapView->FlyTo(*Position, MarkerViewDistance);
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
	int32 Cash = 0;
	int32 Bank = 0;
	const bool bKnowCredits = GetPlayerCredits(Cash, Bank);
	ShownCash = bKnowCredits ? Cash : INDEX_NONE;
	ShownBank = bKnowCredits ? Bank : INDEX_NONE;
	// Core3's PurchaseTicketCommand takes bank first, then cash. It also adds a
	// player-city travel tax the client can't see, so Fare is a lower bound.
	const bool bCanAfford = !bKnowCredits || Cash + Bank >= Fare;

	const FText FareLine = Fare > 0
		? FText::Format(NSLOCTEXT("SWGEmu", "TravelFare", "TICKET  {0} CR"), FText::AsNumber(Fare))
		: FText::GetEmpty();
	const FText CreditsLine = bKnowCredits
		? FText::Format(NSLOCTEXT("SWGEmu", "TravelCredits", "AVAILABLE  {0} CR  (cash {1} · bank {2})"),
			FText::AsNumber(Cash + Bank), FText::AsNumber(Cash), FText::AsNumber(Bank))
		: FText::GetEmpty();
	FareText->SetText(FareLine.IsEmpty() || CreditsLine.IsEmpty()
		? (FareLine.IsEmpty() ? CreditsLine : FareLine)
		: FText::Format(FText::FromString(TEXT("{0}\n{1}")), FareLine, CreditsLine));
	FareText->SetColorAndOpacity(FSlateColor(bCanAfford ? Green : FLinearColor::FromSRGBColor(FColor(0xFF, 0x5A, 0x4A))));
	PurchaseButton->SetIsEnabled(Destination && Fare > 0 && bCanAfford);
	StatusText->SetText(bGalaxyView
		? NSLOCTEXT("SWGEmu", "TravelChoosePlanet", "Choose a planet to view its arrival points.")
		: Destination && Fare > 0 && !bCanAfford
		? FText::Format(NSLOCTEXT("SWGEmu", "TravelInsufficientCredits", "Insufficient credits — you need {0} more."),
			FText::AsNumber(Fare - Cash - Bank))
		: MapView && !MapView->IsTerrainReady()
		? NSLOCTEXT("SWGEmu", "TravelChartingTerrain", "Charting terrain…")
		: Destination
		? FText::Format(NSLOCTEXT("SWGEmu", "TravelCoordinates", "Arrival coordinates: {0}, {1}"),
			FText::AsNumber(FMath::RoundToInt(Destination->Position.X)), FText::AsNumber(FMath::RoundToInt(Destination->Position.Y)))
		: NSLOCTEXT("SWGEmu", "TravelLoading", "Loading available routes…"));
}

bool USWGTravelWidget::GetPlayerCredits(int32& OutCash, int32& OutBank) const
{
	const ASWGPlayer* Player = Cast<ASWGPlayer>(GetOwningPlayerPawn());
	if (!Player)
	{
		return false;
	}
	OutCash = Player->CashCredits;
	OutBank = Player->BankCredits;
	return true;
}

void USWGTravelWidget::HandleGalaxyClicked() { ShowGalaxyMap(); }
void USWGTravelWidget::HandleRoundTripChanged(bool bChecked) { RefreshFare(); }
void USWGTravelWidget::HandleTravelDataChanged() { RefreshTravelData(); }
void USWGTravelWidget::HandleZoomInClicked() { if (MapView) { MapView->ZoomBy(0.5f); } }
void USWGTravelWidget::HandleZoomOutClicked() { if (MapView) { MapView->ZoomBy(2.f); } }

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
		if (MapView)
		{
			MapView->FlyTo(VisibleDestinations[Index].Position, MarkerViewDistance);
		}
		RefreshTravelMarkers();
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
	if (!bGalaxyView && MapView && MapView->HandleControllerKey(Key))
	{
		return FReply::Handled();
	}
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
