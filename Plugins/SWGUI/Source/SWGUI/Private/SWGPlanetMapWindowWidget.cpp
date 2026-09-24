#include "SWGPlanetMapWindowWidget.h"
#include "SWGPlanetMapWidget.h"
#include "SWGRetailStyle.h"
#include "SWGMapMarkers.h"
#include "SWGUISubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Common/SWGWorldScale.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGWaypointSubsystem.h"

namespace
{
	const FName PlayerLayer(TEXT("Player"));
	const FName WaypointLayer(TEXT("Waypoints"));

	FText PlanetDisplayName(const FString& Planet)
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
}

void USWGPlanetMapWindowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetTitle(NSLOCTEXT("SWGEmu", "PlanetMapTitle", "Planetary Map"));

	UGameInstance* GameInstance = GetGameInstance();
	Waypoints = GameInstance ? GameInstance->GetSubsystem<USWGWaypointSubsystem>() : nullptr;
	Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	if (Waypoints)
	{
		Waypoints->OnWaypointListChanged.AddUniqueDynamic(this, &USWGPlanetMapWindowWidget::RefreshWaypointMarkers);
	}

	MapView->OnMarkerClicked.AddUniqueDynamic(this, &USWGPlanetMapWindowWidget::HandleMarkerClicked);
	MapView->OnGroundDoubleClicked.AddUniqueDynamic(this, &USWGPlanetMapWindowWidget::HandleGroundDoubleClicked);
	MapView->OnPressed.AddUniqueDynamic(this, &USWGPlanetMapWindowWidget::HandleMapPressed);

	if (CenterButton) { CenterButton->OnClicked.AddUniqueDynamic(this, &USWGPlanetMapWindowWidget::HandleCenterClicked); }
	if (ZoomInButton) { ZoomInButton->OnClicked.AddUniqueDynamic(this, &USWGPlanetMapWindowWidget::HandleZoomInClicked); }
	if (ZoomOutButton) { ZoomOutButton->OnClicked.AddUniqueDynamic(this, &USWGPlanetMapWindowWidget::HandleZoomOutClicked); }
	if (OverviewButton) { OverviewButton->OnClicked.AddUniqueDynamic(this, &USWGPlanetMapWindowWidget::HandleOverviewClicked); }
	if (HologramButton) { HologramButton->OnClicked.AddUniqueDynamic(this, &USWGPlanetMapWindowWidget::HandleHologramClicked); }
	if (ButtonTextTints.IsEmpty())
	{
		for (UButton* Button : { CenterButton.Get(), ZoomInButton.Get(), ZoomOutButton.Get(), OverviewButton.Get(), HologramButton.Get() })
		{
			if (UObject* TextTint = SWGRetailStyle::ApplyHudButton(Button, Tre))
			{
				ButtonTextTints.Add(TextTint);
			}
		}
	}
	if (StatusText)
	{
		StatusText->SetText(bCreateWaypointOnDoubleClick
			? NSLOCTEXT("SWGEmu", "PlanetMapHint", "Drag to pan, right-drag to turn. Double-click to set a waypoint.")
			: NSLOCTEXT("SWGEmu", "PlanetMapHintNoWaypoint", "Drag to pan, right-drag to turn."));
	}

	RefreshPlanet();
	RefreshWaypointMarkers();
	SetControllerMode(bControllerMode);
}

void USWGPlanetMapWindowWidget::NativeDestruct()
{
	if (Waypoints)
	{
		Waypoints->OnWaypointListChanged.RemoveDynamic(this, &USWGPlanetMapWindowWidget::RefreshWaypointMarkers);
	}
	Super::NativeDestruct();
}

void USWGPlanetMapWindowWidget::SetControllerMode(bool bEnabled)
{
	bControllerMode = bEnabled;
	if (IsConstructed() && bControllerMode)
	{
		SetFocus();
	}
}

bool USWGPlanetMapWindowWidget::GetPlayerPosition(FVector2D& OutPosition, float& OutHeading) const
{
	const APawn* Pawn = GetOwningPlayerPawn();
	if (!Pawn)
	{
		return false;
	}
	const FVector Raw = SWGToRawSpace(Pawn->GetActorLocation());
	OutPosition = FVector2D(Raw.X, Raw.Y);
	// UE yaw 0 is +X, which is north here, and turns toward east (+Y).
	OutHeading = Pawn->GetActorRotation().Yaw;
	return true;
}

void USWGPlanetMapWindowWidget::RefreshPlanet()
{
	const USWGTerrainSubsystem* Terrain = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
	Planet = Terrain ? Terrain->GetActivePlanetName().ToLower() : FString();
	FVector2D Position;
	float Heading = 0.f;
	GetPlayerPosition(Position, Heading);
	BuildingFocus = Position;
	MapView->ShowPlanet(Planet, { BuildingFocus });
	if (bStartOnPlayer)
	{
		MapView->JumpTo(Position, FocusDistance);
	}
}

void USWGPlanetMapWindowWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	FVector2D Position;
	float Heading = 0.f;
	if (!GetPlayerPosition(Position, Heading))
	{
		return;
	}

	FSWGMapMarker Self;
	if (SWGMapMarkers::MakePlayerMarker(GetOwningPlayerPawn(), Self))
	{
		MapView->UpdateMarker(PlayerLayer, Self);
	}

	if (FVector2D::Distance(Position, BuildingFocus) > BuildingRefocusDistance)
	{
		BuildingFocus = Position;
		MapView->ShowPlanet(Planet, { BuildingFocus });
	}
	if (LocationText)
	{
		LocationText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "PlanetMapLocation", "{0}   {1}, {2}"),
			PlanetDisplayName(Planet), FText::AsNumber(FMath::RoundToInt(Position.X)), FText::AsNumber(FMath::RoundToInt(Position.Y))));
	}
}

void USWGPlanetMapWindowWidget::RefreshWaypointMarkers()
{
	const TArray<FSWGMapMarker> Markers = SWGMapMarkers::MakeWaypointMarkers(Waypoints, Planet);
	WaypointPositions.Reset();
	for (const FSWGMapMarker& Marker : Markers)
	{
		WaypointPositions.Add(Marker.Id, Marker.Position);
	}
	MapView->SetMarkers(WaypointLayer, Markers);
}

void USWGPlanetMapWindowWidget::CenterOnPlayer()
{
	FVector2D Position;
	float Heading = 0.f;
	if (GetPlayerPosition(Position, Heading))
	{
		MapView->FlyTo(Position, FocusDistance);
	}
}

void USWGPlanetMapWindowWidget::HandleMarkerClicked(FName Layer, FName MarkerId)
{
	if (const FVector2D* Position = WaypointPositions.Find(MarkerId))
	{
		MapView->FlyTo(*Position, FocusDistance);
	}
	else if (Layer == PlayerLayer)
	{
		CenterOnPlayer();
	}
}

void USWGPlanetMapWindowWidget::HandleGroundDoubleClicked(FVector2D RawPosition)
{
	USWGCommandSubsystem* Commands = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGCommandSubsystem>() : nullptr;
	if (!bCreateWaypointOnDoubleClick || !Commands)
	{
		return;
	}
	// Core3 WaypointCommand, ground usage: "/waypoint X Y".
	Commands->SendCommand(TEXT("waypoint"), 0, FString::Printf(TEXT("%d %d"), FMath::RoundToInt(RawPosition.X), FMath::RoundToInt(RawPosition.Y)));
	if (StatusText)
	{
		StatusText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "PlanetMapWaypointSent", "Waypoint requested at {0}, {1}."),
			FText::AsNumber(FMath::RoundToInt(RawPosition.X)), FText::AsNumber(FMath::RoundToInt(RawPosition.Y))));
	}
}

void USWGPlanetMapWindowWidget::HandleMapPressed() { OnPressed.Broadcast(this); }
void USWGPlanetMapWindowWidget::HandleCenterClicked() { CenterOnPlayer(); }
void USWGPlanetMapWindowWidget::HandleZoomInClicked() { MapView->ZoomBy(0.5f); }
void USWGPlanetMapWindowWidget::HandleZoomOutClicked() { MapView->ZoomBy(2.f); }
void USWGPlanetMapWindowWidget::HandleOverviewClicked() { MapView->ResetView(); }

void USWGPlanetMapWindowWidget::HandleHologramClicked()
{
	if (USWGUISubsystem* UI = ULocalPlayer::GetSubsystem<USWGUISubsystem>(GetOwningLocalPlayer()))
	{
		UI->SetPlanetMapMode(ESWGPlanetMapMode::Hologram);
	}
}

FReply USWGPlanetMapWindowWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (bControllerMode && MapView->HandleControllerKey(Key))
	{
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Left)
	{
		CenterOnPlayer();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Top)
	{
		MapView->ResetView();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
