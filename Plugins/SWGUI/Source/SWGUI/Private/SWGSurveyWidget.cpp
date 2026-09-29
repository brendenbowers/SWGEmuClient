#include "SWGSurveyWidget.h"
#include "SWGMapMarkers.h"
#include "SWGPlanetMapWidget.h"
#include "SWGRetailStyle.h"
#include "SWGSurveyStyle.h"
#include "SWGTravelWidget.h"
#include "SWGUISubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Common/SWGWorldScale.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "Subsystems/SWGSurveySubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGWaypointSubsystem.h"

namespace
{
	const FName SurveyLayer = SWGSurveyStyle::MarkerLayer;
	const FName SurveyWaypointLayer(TEXT("Waypoints"));
	const FName SurveyPlayerLayer(TEXT("Player"));
	const FName BestStyle = SWGSurveyStyle::BestStyle;

	/** ui_res_survey.inc: header #97FFFF, text1 #96F4FC. */
	const FLinearColor HeaderColor = FLinearColor::FromSRGBColor(FColor(0x97, 0xFF, 0xFF));
	const FLinearColor RowTextColor = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));
	const FLinearColor RowSelectedFill = FLinearColor::FromSRGBColor(FColor(0x00, 0xD6, 0xFB, 0x70));
	const FLinearColor RowHoverFill = FLinearColor::FromSRGBColor(FColor(0x00, 0xD6, 0xFB, 0x30));

	/** The server has no survey smaller than the 64 m tool range. */
	constexpr float MinimumFramedRange = 64.f;

	FButtonStyle MakeRowStyle(bool bSelected)
	{
		FButtonStyle Style;
		const FLinearColor Idle = bSelected ? RowSelectedFill : FLinearColor::Transparent;
		Style.SetNormal(FSlateColorBrush(Idle));
		Style.SetHovered(FSlateColorBrush(bSelected ? RowSelectedFill : RowHoverFill));
		Style.SetPressed(FSlateColorBrush(RowSelectedFill));
		Style.SetDisabled(FSlateColorBrush(FLinearColor::Transparent));
		Style.SetNormalPadding(FMargin(6.f, 2.f));
		Style.SetPressedPadding(FMargin(6.f, 2.f));
		return Style;
	}

	FText Retail(USWGTreSubsystem* Tre, const TCHAR* Reference, const TCHAR* Fallback)
	{
		const FString Resolved = Tre ? Tre->ResolveStringId(Reference) : FString();
		return FText::FromString(Resolved.IsEmpty() || Resolved == Reference ? FString(Fallback) : Resolved);
	}
}

// ── Survey point marker ──────────────────────────────────────────────────────

void USWGSurveyMarkerWidget::NativeOnInitialized()
{
	if (!WidgetTree->RootWidget)
	{
		LabelText = WidgetTree->ConstructWidget<UTextBlock>();
		LabelText->SetShadowOffset(FVector2D(1.f, 1.f));
		LabelText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.95f));
		LabelText->SetJustification(ETextJustify::Center);
		LabelText->SetVisibility(ESlateVisibility::HitTestInvisible);
		WidgetTree->RootWidget = LabelText;
	}
	Super::NativeOnInitialized();
	Anchor = FVector2D(0.5f, 0.5f);
}

void USWGSurveyMarkerWidget::ApplyMarker_Implementation(const FSWGMapMarker& InMarker)
{
	const bool bBest = InMarker.Style == BestStyle;
	if (LabelText)
	{
		LabelText->SetText(bBest ? FText::Format(NSLOCTEXT("SWGEmu", "SurveyBestLabel", "◎ {0}"), InMarker.Label) : InMarker.Label);
		FLinearColor Color = InMarker.LabelColor;
		Color.A = 1.f;
		LabelText->SetColorAndOpacity(FSlateColor(Color));
		LabelText->SetFont(SWGRetailStyle::Font(bBest ? 18 : 13));
	}
	Anchor = FVector2D(0.5f, 0.5f);
}

// ── Window ───────────────────────────────────────────────────────────────────

void USWGSurveyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UGameInstance* GameInstance = GetGameInstance();
	Survey = GameInstance ? GameInstance->GetSubsystem<USWGSurveySubsystem>() : nullptr;
	Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	Waypoints = GameInstance ? GameInstance->GetSubsystem<USWGWaypointSubsystem>() : nullptr;
	SetTitle(Retail(Tre, TEXT("@ui:cpt_surveying"), TEXT("Surveying")));

	if (Survey)
	{
		Survey->OnResourcesChanged.AddUniqueDynamic(this, &USWGSurveyWidget::HandleResourcesChanged);
		Survey->OnSurveyStateChanged.AddUniqueDynamic(this, &USWGSurveyWidget::HandleSurveyStateChanged);
		Survey->OnSurveyResultReceived.AddUniqueDynamic(this, &USWGSurveyWidget::HandleResultReceived);
	}
	if (Waypoints)
	{
		Waypoints->OnWaypointListChanged.AddUniqueDynamic(this, &USWGSurveyWidget::HandleWaypointsChanged);
	}

	SurveyButton->OnClicked.AddUniqueDynamic(this, &USWGSurveyWidget::HandleSurveyClicked);
	SampleButton->OnClicked.AddUniqueDynamic(this, &USWGSurveyWidget::HandleSampleClicked);
	if (RangeButton) { RangeButton->OnClicked.AddUniqueDynamic(this, &USWGSurveyWidget::HandleRangeClicked); }
	if (CenterButton) { CenterButton->OnClicked.AddUniqueDynamic(this, &USWGSurveyWidget::HandleCenterClicked); }
	if (HologramButton) { HologramButton->OnClicked.AddUniqueDynamic(this, &USWGSurveyWidget::HandleHologramClicked); }
	if (ButtonTextTints.IsEmpty())
	{
		const TPair<UButton*, FString> Buttons[] = {
			{ SurveyButton.Get(), TEXT("@ui:res_survey") }, { SampleButton.Get(), TEXT("@ui:res_survey_sample") },
			{ RangeButton.Get(), FString() }, { CenterButton.Get(), FString() }, { HologramButton.Get(), FString() } };
		for (const TPair<UButton*, FString>& Entry : Buttons)
		{
			if (UObject* TextTint = SWGRetailStyle::ApplyHudButton(Entry.Key, Tre, Entry.Value))
			{
				ButtonTextTints.Add(TextTint);
			}
		}
	}

	MapView->SetLayerMarkerClass(SurveyLayer, SurveyMarkerClass ? SurveyMarkerClass : TSubclassOf<USWGMapMarkerWidget>(USWGSurveyMarkerWidget::StaticClass()));
	MapView->OnPressed.AddUniqueDynamic(this, &USWGSurveyWidget::HandleMapPressed);
	const USWGTerrainSubsystem* Terrain = GameInstance ? GameInstance->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
	Planet = Terrain ? Terrain->GetActivePlanetName().ToLower() : FString();
	FVector2D PlayerPosition;
	GetPlayerPosition(PlayerPosition);
	MapView->ShowPlanet(Planet, { PlayerPosition });
	FrameSurveyArea(true);

	RebuildResourceList();
	RefreshResult();
	RefreshWaypoints();
	RefreshStatus();
}

void USWGSurveyWidget::NativeDestruct()
{
	if (Survey)
	{
		Survey->OnResourcesChanged.RemoveAll(this);
		Survey->OnSurveyStateChanged.RemoveAll(this);
		Survey->OnSurveyResultReceived.RemoveAll(this);
	}
	if (Waypoints)
	{
		Waypoints->OnWaypointListChanged.RemoveAll(this);
	}
	Super::NativeDestruct();
}

void USWGSurveyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	FSWGMapMarker Self;
	if (SWGMapMarkers::MakePlayerMarker(GetOwningPlayerPawn(), Self))
	{
		MapView->UpdateMarker(SurveyPlayerLayer, Self);
	}
	// Enabled state follows the subsystem, which may change without an event (a list arriving mid-survey).
	const bool bHasSelection = Survey && !Survey->GetSelectedResource().IsEmpty();
	SurveyButton->SetIsEnabled(bHasSelection && !Survey->IsSurveyPending());
	SampleButton->SetIsEnabled(bHasSelection);
	if (RangeButton)
	{
		RangeButton->SetIsEnabled(Survey && Survey->GetToolObjectId() != 0);
	}
	const int32 Range = Survey ? Survey->GetCurrentRange() : 0;
	RangeText->SetText(Range > 0
		? FText::Format(NSLOCTEXT("SWGEmu", "SurveyRangeValue", "{0}m"), FText::AsNumber(Range))
		: NSLOCTEXT("SWGEmu", "SurveyRangeUnknown", "—"));
}

bool USWGSurveyWidget::GetPlayerPosition(FVector2D& OutPosition) const
{
	const APawn* Pawn = GetOwningPlayerPawn();
	if (!Pawn)
	{
		OutPosition = FVector2D::ZeroVector;
		return false;
	}
	const FVector Raw = SWGToRawSpace(Pawn->GetActorLocation());
	OutPosition = FVector2D(Raw.X, Raw.Y);
	return true;
}

void USWGSurveyWidget::FrameSurveyArea(bool bJump)
{
	FVector2D Center;
	GetPlayerPosition(Center);
	float Range = Survey ? static_cast<float>(Survey->GetCurrentRange()) : 0.f;
	if (Survey && Survey->HasResult())
	{
		Center = Survey->GetLastResult().Center;
		Range = Survey->GetLastResult().Range;
	}
	const float Distance = FMath::Max(Range, MinimumFramedRange) * ViewDistancePerRange;
	if (bJump)
	{
		MapView->JumpTo(Center, Distance, true);
	}
	else
	{
		MapView->FlyTo(Center, Distance, true);
	}
}

void USWGSurveyWidget::RebuildResourceList()
{
	ResourceList->ClearChildren();
	ResourceButtons.Reset();
	ResourceButtonNames.Reset();
	ResourceClickForwarders.Reset();
	if (ResourceTitleText)
	{
		ResourceTitleText->SetText(Survey && !Survey->GetSurveyType().IsEmpty()
			? Survey->GetSurveyTypeDisplayName()
			: Retail(Tre, TEXT("@ui:res_survey_resnames"), TEXT("Resources")));
	}
	if (!Survey)
	{
		return;
	}

	FString CurrentClass;
	for (const FSWGSurveyResource& Resource : Survey->GetResources())
	{
		if (Resource.ClassName != CurrentClass)
		{
			CurrentClass = Resource.ClassName;
			UTextBlock* Header = WidgetTree->ConstructWidget<UTextBlock>();
			Header->SetText(FText::FromString(CurrentClass));
			Header->SetFont(SWGRetailStyle::Font(12));
			Header->SetColorAndOpacity(FSlateColor(HeaderColor));
			Header->SetToolTipText(FText::FromString(FString::Join(Resource.ClassPath, TEXT(" > "))));
			ResourceList->AddChild(Header);
		}

		UButton* Row = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Resource.Name));
		Label->SetFont(SWGRetailStyle::Font(12, false));
		Label->SetColorAndOpacity(FSlateColor(RowTextColor));
		if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(Row->AddChild(Label)))
		{
			LabelSlot->SetHorizontalAlignment(HAlign_Left);
		}
		ResourceList->AddChild(Row);

		USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
		const FString Name = Resource.Name;
		Forwarder->Action = [this, Name]()
		{
			if (Survey)
			{
				Survey->SetSelectedResource(Name);
			}
		};
		Row->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
		ResourceClickForwarders.Add(Forwarder);
		ResourceButtons.Add(Row);
		ResourceButtonNames.Add(Resource.Name);
	}
	RefreshSelection();
}

void USWGSurveyWidget::RefreshSelection()
{
	const FString Selected = Survey ? Survey->GetSelectedResource() : FString();
	for (int32 Index = 0; Index < ResourceButtons.Num(); ++Index)
	{
		ResourceButtons[Index]->SetStyle(MakeRowStyle(ResourceButtonNames[Index] == Selected));
	}
}

void USWGSurveyWidget::RefreshResult()
{
	if (!Survey || !Survey->HasResult())
	{
		MapView->ClearLayer(SurveyLayer);
		MapView->ClearGroundOverlay();
		return;
	}
	const FSWGSurveyResult& Result = Survey->GetLastResult();
	MapView->SetMarkers(SurveyLayer, SWGSurveyStyle::MakeMarkers(Result));
	MapView->SetGroundOverlay(SWGSurveyStyle::MakeOverlay(Result, 24, OverlayOpacity));
}

void USWGSurveyWidget::RefreshWaypoints()
{
	MapView->SetMarkers(SurveyWaypointLayer, SWGMapMarkers::MakeWaypointMarkers(Waypoints, Planet));
}

void USWGSurveyWidget::RefreshStatus()
{
	if (!StatusText)
	{
		return;
	}
	FText Status;
	if (!Survey || Survey->GetResources().IsEmpty())
	{
		Status = Retail(Tre, TEXT("@ui:survey_noresource"), TEXT("There are no resources of this type on this planet."));
	}
	else if (Survey->IsSurveyPending())
	{
		Status = FText::Format(NSLOCTEXT("SWGEmu", "SurveyPending", "Surveying for {0}..."), FText::FromString(Survey->GetSelectedResource()));
	}
	else if (!TransientStatus.IsEmpty())
	{
		Status = TransientStatus;
	}
	else if (Survey->HasResult())
	{
		const FSWGSurveyResult& Result = Survey->GetLastResult();
		const float Best = Result.GetBest().Density;
		Status = Best >= FSWGSurveyResult::WaypointDensity
			? FText::Format(NSLOCTEXT("SWGEmu", "SurveyBest", "{0}: highest concentration {1}. Waypoint set."),
				FText::FromString(Result.ResourceName), SWGSurveyStyle::DensityText(Best))
			: Retail(Tre, TEXT("@ui:survey_nothingfound"), TEXT("Nothing was found in this area."));
	}
	else
	{
		Status = Retail(Tre, TEXT("@ui:survey_select_resource"), TEXT("Select a resource and press Survey."));
	}
	StatusText->SetText(Status);
}

void USWGSurveyWidget::HandleResourcesChanged()
{
	RebuildResourceList();
	RefreshStatus();
}

void USWGSurveyWidget::HandleSurveyStateChanged()
{
	TransientStatus = FText::GetEmpty();
	RefreshSelection();
	RefreshStatus();
}

void USWGSurveyWidget::HandleResultReceived()
{
	RefreshResult();
	FrameSurveyArea(false);
	RefreshStatus();
}

void USWGSurveyWidget::HandleWaypointsChanged()
{
	RefreshWaypoints();
}

void USWGSurveyWidget::HandleSurveyClicked()
{
	if (Survey)
	{
		// The grid is centred wherever the player stands now.
		FrameSurveyArea(false);
		Survey->RequestSurvey();
	}
}

void USWGSurveyWidget::HandleSampleClicked()
{
	if (Survey && Survey->RequestSample())
	{
		TransientStatus = FText::Format(NSLOCTEXT("SWGEmu", "SurveySampling", "Sampling for {0}..."), FText::FromString(Survey->GetSelectedResource()));
		RefreshStatus();
	}
}

void USWGSurveyWidget::HandleRangeClicked()
{
	if (Survey)
	{
		Survey->RequestRangeSettings();
	}
}

void USWGSurveyWidget::HandleCenterClicked()
{
	FVector2D Position;
	if (GetPlayerPosition(Position))
	{
		MapView->FlyTo(Position, FMath::Max(static_cast<float>(Survey ? Survey->GetCurrentRange() : 0), MinimumFramedRange) * ViewDistancePerRange);
	}
}

void USWGSurveyWidget::HandleHologramClicked()
{
	if (USWGUISubsystem* UI = ULocalPlayer::GetSubsystem<USWGUISubsystem>(GetOwningLocalPlayer()))
	{
		UI->SetSurveyMode(ESWGSurveyMode::Hologram);
	}
}

void USWGSurveyWidget::HandleMapPressed()
{
	OnPressed.Broadcast(this);
}
