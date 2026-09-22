#include "SWGWaypointMarkerWidget.h"
#include "Subsystems/SWGWaypointSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"

namespace
{
	/** Best-effort retail icon for a waypoint pin; falls back to a plain tinted square when the style sheet has nothing under these paths. */
	FSlateBrush ResolveWaypointIconBrush(USWGTreSubsystem* Tre)
	{
		if (Tre)
		{
			const FSWGUIStyleSheet* Sheet = Tre->GetUIStyleSheet();
			static const TCHAR* CandidatePaths[] = { TEXT("icon.waypoint"), TEXT("icon.map.waypoint"), TEXT("icon.datapad.waypoint") };
			const FSWGUIImageStyle* Style = nullptr;
			for (const TCHAR* Path : CandidatePaths)
			{
				if (Sheet && (Style = Sheet->FindImageStyle(Path)) != nullptr)
				{
					break;
				}
			}

			if (Style)
			{
				if (UTexture2D* Sheet2D = Tre->GetOrLoadTexture(FString::Printf(TEXT("texture/%s.dds"), *Style->Source)))
				{
					const FVector2D SheetSize(Sheet2D->GetSizeX(), Sheet2D->GetSizeY());
					if (SheetSize.X > 0.f && SheetSize.Y > 0.f)
					{
						FSlateBrush Brush;
						Brush.SetResourceObject(Sheet2D);
						Brush.ImageSize = FVector2D(Style->SourceRect.Size());
						Brush.SetUVRegion(FBox2D(FVector2D(Style->SourceRect.Min) / SheetSize, FVector2D(Style->SourceRect.Max) / SheetSize));
						return Brush;
					}
				}
			}
		}

		FSlateBrush Fallback = *FCoreStyle::Get().GetBrush("WhiteBrush");
		Fallback.DrawAs = ESlateBrushDrawType::Box;
		Fallback.Tiling = ESlateBrushTileType::NoTile;
		return Fallback;
	}
}

void USWGWaypointMarkerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USWGWaypointSubsystem* Waypoints = GameInstance->GetSubsystem<USWGWaypointSubsystem>())
		{
			Waypoints->OnWaypointListChanged.AddUniqueDynamic(this, &USWGWaypointMarkerWidget::HandleWaypointListChanged);
		}
	}
}

void USWGWaypointMarkerWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USWGWaypointSubsystem* Waypoints = GameInstance->GetSubsystem<USWGWaypointSubsystem>())
		{
			Waypoints->OnWaypointListChanged.RemoveDynamic(this, &USWGWaypointMarkerWidget::HandleWaypointListChanged);
		}
	}
	Markers.Reset();
	Super::NativeDestruct();
}

void USWGWaypointMarkerWidget::HandleWaypointListChanged()
{
	// The next NativeTick rebuilds from GetActiveWaypoints(); nothing to do here
	// beyond existing — stale markers for waypoints that dropped out are culled there.
}

USWGWaypointMarkerWidget::FMarkerWidgets& USWGWaypointMarkerWidget::FindOrCreateMarker(int64 WaypointObjectId)
{
	if (FMarkerWidgets* Existing = Markers.Find(WaypointObjectId))
	{
		return *Existing;
	}

	FMarkerWidgets Marker;

	Marker.Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	Marker.Icon->SetDesiredSizeOverride(IconSize);

	Marker.Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Marker.Label->SetShadowOffset(FVector2D(1.f, 1.f));
	Marker.Label->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
	FSlateFontInfo Font = Marker.Label->GetFont();
	Font.Size = 14;
	Font.OutlineSettings.OutlineSize = 1;
	Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
	Marker.Label->SetFont(Font);

	UCanvasPanelSlot* IconSlot = Canvas->AddChildToCanvas(Marker.Icon);
	IconSlot->SetAutoSize(true);
	IconSlot->SetAlignment(FVector2D(0.5f, 0.5f));

	UCanvasPanelSlot* LabelSlot = Canvas->AddChildToCanvas(Marker.Label);
	LabelSlot->SetAutoSize(true);
	LabelSlot->SetAlignment(FVector2D(0.5f, 0.f));

	return Markers.Add(WaypointObjectId, Marker);
}

void USWGWaypointMarkerWidget::UpdateMarker(FMarkerWidgets& Marker, const FSWGWaypointEntry& Entry, const FVector2D& ViewportSize)
{
	APlayerController* PlayerController = GetOwningPlayer();
	APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager : nullptr;
	if (!PlayerController || !CameraManager)
	{
		Marker.Icon->SetVisibility(ESlateVisibility::Collapsed);
		Marker.Label->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	const FLinearColor Color = USWGWaypointSubsystem::GetWaypointColor(Entry.Color);
	Marker.Icon->SetColorAndOpacity(Color);

	const FVector WorldPosition = SWGToUnrealSpace(Entry.RawPosition) + FVector(0.f, 0.f, MarkerHeightAboveGround);
	const FVector CameraLocation = CameraManager->GetCameraLocation();
	const FVector ToTarget = WorldPosition - CameraLocation;
	const bool bInFront = FVector::DotProduct(CameraManager->GetCameraRotation().Vector(), ToTarget) > 0.f;

	FVector2D ScreenPosition = FVector2D::ZeroVector;
	const bool bProjected = bInFront && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PlayerController, WorldPosition, ScreenPosition, false);
	const bool bOnScreen = bProjected && Entry.bIsWorldLoaded
		&& ScreenPosition.X >= 0.f && ScreenPosition.X <= ViewportSize.X
		&& ScreenPosition.Y >= 0.f && ScreenPosition.Y <= ViewportSize.Y;

	float ArrowRotationDegrees = 0.f;
	if (!bOnScreen)
	{
		// Camera-yaw-relative bearing: UE yaw 0 already lands on raw north (see
		// SWGWorldScale.h), so this stays correct even with the target directly
		// behind the player, where a 3D projection alone would not.
		float Relative = Entry.BearingDegrees - CameraManager->GetCameraRotation().Yaw;
		Relative = FRotator::NormalizeAxis(Relative);
		ArrowRotationDegrees = Relative;

		const float RelativeRadians = FMath::DegreesToRadians(Relative);
		const FVector2D Direction(FMath::Sin(RelativeRadians), -FMath::Cos(RelativeRadians));
		const FVector2D Center = ViewportSize * 0.5f;
		const FVector2D HalfExtent = Center - FVector2D(EdgeMargin, EdgeMargin);

		const float ScaleX = FMath::IsNearlyZero(Direction.X) ? BIG_NUMBER : HalfExtent.X / FMath::Abs(Direction.X);
		const float ScaleY = FMath::IsNearlyZero(Direction.Y) ? BIG_NUMBER : HalfExtent.Y / FMath::Abs(Direction.Y);
		ScreenPosition = Center + Direction * FMath::Min(ScaleX, ScaleY);
	}

	if (UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(Marker.Icon->Slot))
	{
		IconSlot->SetPosition(ScreenPosition);
	}
	if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Marker.Label->Slot))
	{
		LabelSlot->SetPosition(ScreenPosition + FVector2D(0.f, IconSize.Y * 0.5f + 2.f));
	}

	// When the waypoint is visible and loaded, ASWGWaypointMarker's own 3D
	// beacon is what's actually drawing it (correctly occluded by geometry,
	// unlike this screen-space icon) — showing the icon on top of it here too
	// would just double it up, so only the floating name/distance label stays.
	Marker.Icon->SetRenderTransformAngle(ArrowRotationDegrees);
	Marker.Icon->SetVisibility(bOnScreen ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	Marker.Label->SetVisibility(ESlateVisibility::HitTestInvisible);
	Marker.Label->SetText(Entry.bHasDistance
		? FText::Format(NSLOCTEXT("SWGWaypoint", "MarkerLabel", "{0} ({1}m)"), Entry.Name, FText::AsNumber(FMath::RoundToInt(Entry.DistanceMeters)))
		: Entry.Name);
	Marker.Label->SetColorAndOpacity(FSlateColor(Color));
}

void USWGWaypointMarkerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UGameInstance* GameInstance = GetGameInstance();
	USWGWaypointSubsystem* Waypoints = GameInstance ? GameInstance->GetSubsystem<USWGWaypointSubsystem>() : nullptr;
	if (!Waypoints)
	{
		return;
	}

	if (!bResolvedIconBrush)
	{
		IconBrush = ResolveWaypointIconBrush(GameInstance->GetSubsystem<USWGTreSubsystem>());
		bResolvedIconBrush = true;
	}

	const TArray<FSWGWaypointEntry> Active = Waypoints->GetActiveWaypoints();
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);

	TSet<int64> Seen;
	Seen.Reserve(Active.Num());
	for (const FSWGWaypointEntry& Entry : Active)
	{
		Seen.Add(Entry.WaypointObjectId);
		FMarkerWidgets& Marker = FindOrCreateMarker(Entry.WaypointObjectId);
		Marker.Icon->SetBrush(IconBrush);
		UpdateMarker(Marker, Entry, ViewportSize);
	}

	for (auto It = Markers.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It->Key))
		{
			It->Value.Icon->RemoveFromParent();
			It->Value.Label->RemoveFromParent();
			It.RemoveCurrent();
		}
	}
}
