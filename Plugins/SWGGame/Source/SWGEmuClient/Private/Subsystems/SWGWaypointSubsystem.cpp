#include "Subsystems/SWGWaypointSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGMissionSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Components/SWGJournalComponent.h"
#include "Network/Objects/Zone/Player/Waypoint.h"
#include "Common/SWGWorldScale.h"
#include "Objects/World/SWGWaypointMarker.h"
#include "Objects/World/SWGWaypointCompassArrow.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

namespace
{
	/** 8-point compass bearing from PlayerRaw to TargetRaw, both in raw (x east, y north) space. Empty/zero if they're on top of each other. Mirrors USWGMissionSubsystem's own copy of this. */
	FString CompassDirection(const FVector& PlayerRaw, const FVector& TargetRaw, float& OutBearingDegrees)
	{
		const float East = TargetRaw.X - PlayerRaw.X;
		const float North = TargetRaw.Y - PlayerRaw.Y;
		if (FMath::IsNearlyZero(East) && FMath::IsNearlyZero(North))
		{
			OutBearingDegrees = 0.f;
			return FString();
		}

		float Bearing = FMath::RadiansToDegrees(FMath::Atan2(East, North));
		if (Bearing < 0.f) { Bearing += 360.f; }
		OutBearingDegrees = Bearing;

		static const TCHAR* Points[] = { TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW") };
		const int32 Index = FMath::RoundToInt(Bearing / 45.f) % 8;
		return Points[Index];
	}

	/**
	 * A waypoint's WaypointName often arrives as a raw "@table:key" StringId
	 * reference (mission-granted ones do: e.g. "@mission/mission_destroy_
	 * neutral_medium_creature:m24t") rather than literal display text —
	 * resolve it the same way chat/system messages do. A name without ':'
	 * is already literal text and passes through unchanged.
	 */
	FText ResolveWaypointName(USWGTreSubsystem* Tre, const FString& RawName)
	{
		return FText::FromString(Tre ? Tre->ResolveStringId(RawName) : RawName);
	}

	/** Identity/display fields only — what PollForChanges compares to decide whether anything actually changed. */
	bool EntriesMatch(const FSWGWaypointEntry& A, const FSWGWaypointEntry& B)
	{
		return A.WaypointObjectId == B.WaypointObjectId
			&& A.Name.EqualTo(B.Name)
			&& A.PlanetCRC == B.PlanetCRC
			&& A.Color == B.Color
			&& A.bActive == B.bActive
			&& A.CellId == B.CellId
			&& A.RawPosition.Equals(B.RawPosition, 0.01f);
	}
}

DEFINE_LOG_CATEGORY_STATIC(LogSWGWaypoint, Log, All);

void USWGWaypointSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ObjectGraph = Cast<USWGObjectGraphSubsystem>(Collection.InitializeDependency(USWGObjectGraphSubsystem::StaticClass()));
	Terrain = Cast<USWGTerrainSubsystem>(Collection.InitializeDependency(USWGTerrainSubsystem::StaticClass()));
	Missions = Cast<USWGMissionSubsystem>(Collection.InitializeDependency(USWGMissionSubsystem::StaticClass()));
	Tre = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));

	if (Missions)
	{
		Missions->OnMissionListChanged.AddDynamic(this, &USWGWaypointSubsystem::HandleMissionListChanged);
	}

	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		World->GetTimerManager().SetTimer(PollTimer, this, &USWGWaypointSubsystem::PollForChanges, PollInterval, true);
	}
}

void USWGWaypointSubsystem::Deinitialize()
{
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}

	if (Missions)
	{
		Missions->OnMissionListChanged.RemoveDynamic(this, &USWGWaypointSubsystem::HandleMissionListChanged);
	}

	if (IsValid(CompassArrow))
	{
		CompassArrow->Destroy();
	}
	CompassArrow = nullptr;

	for (const TPair<int64, TObjectPtr<ASWGWaypointMarker>>& Pair : Markers)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}
	Markers.Reset();

	Super::Deinitialize();
}

bool USWGWaypointSubsystem::IsTickable() const
{
	return GetGameInstance() != nullptr;
}

TStatId USWGWaypointSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGWaypointSubsystem, STATGROUP_Tickables);
}

void USWGWaypointSubsystem::Tick(float DeltaTime)
{
	const TArray<FSWGWaypointEntry> Active = GetActiveWaypoints();
	UpdateMarkers(Active);
	UpdateCompassArrow(Active);
}

USWGJournalComponent* USWGWaypointSubsystem::FindJournalComponent() const
{
	return ObjectGraph ? ObjectGraph->FindComponent<USWGJournalComponent>(ObjectGraph->GetLocalPlayerObjectId()) : nullptr;
}

void USWGWaypointSubsystem::PollForChanges()
{
	const USWGJournalComponent* Journal = FindJournalComponent();
	if (!Journal || !Journal->bHasBase8)
	{
		return;
	}

	TArray<FSWGWaypointEntry> NewEntries;
	NewEntries.Reserve(Journal->WaypointList.Items.Num());
	for (const FWaypoint& Waypoint : Journal->WaypointList.Items)
	{
		FSWGWaypointEntry Entry;
		Entry.WaypointObjectId = Waypoint.WaypointObjectId;
		Entry.Name = ResolveWaypointName(Tre, Waypoint.WaypointName);
		Entry.PlanetCRC = static_cast<int32>(Waypoint.PlanetCRC);
		Entry.Color = static_cast<ESWGWaypointColor>(FMath::Clamp<uint8>(Waypoint.Colour, 0, 5));
		Entry.bActive = Waypoint.Active != 0;
		Entry.CellId = Waypoint.CellId;
		// Wire order is X, Z, Y (native x east/y up/z north); the struct's own
		// field names are already the raw (x east, y north, z up) frame.
		Entry.RawPosition = FVector(Waypoint.XCoord, Waypoint.YCoord, Waypoint.ZCoord);
		NewEntries.Add(Entry);
	}

	bool bChanged = NewEntries.Num() != CachedEntries.Num();
	if (!bChanged)
	{
		for (int32 Index = 0; Index < NewEntries.Num(); ++Index)
		{
			if (!EntriesMatch(NewEntries[Index], CachedEntries[Index]))
			{
				bChanged = true;
				break;
			}
		}
	}

	if (bChanged)
	{
		CachedEntries = MoveTemp(NewEntries);
		OnWaypointListChanged.Broadcast();
	}
}

void USWGWaypointSubsystem::HandleMissionListChanged()
{
	// GetMissionWaypoints() reads USWGMissionSubsystem::GetMissions() fresh on every
	// GetWaypoints() call — nothing to cache here, just tell listeners to re-read.
	OnWaypointListChanged.Broadcast();
}

TArray<FSWGWaypointEntry> USWGWaypointSubsystem::GetMissionWaypoints() const
{
	TArray<FSWGWaypointEntry> Result;
	if (!Missions)
	{
		return Result;
	}

	for (const FSWGMissionEntry& Mission : Missions->GetMissionsWithWaypoints())
	{
		if (!Mission.bHasWaypoint || Mission.WaypointObjectId == 0)
		{
			continue;
		}

		FSWGWaypointEntry Entry;
		Entry.WaypointObjectId = Mission.WaypointObjectId;
		Entry.Name = ResolveWaypointName(Tre, Mission.WaypointName);
		Entry.PlanetCRC = Mission.WaypointPlanetCrc;
		Entry.Color = static_cast<ESWGWaypointColor>(FMath::Clamp<uint8>(Mission.WaypointColor, 0, 5));
		Entry.bActive = Mission.bWaypointActive;
		Entry.RawPosition = Mission.WaypointRawPosition;
		Result.Add(Entry);
	}
	return Result;
}

TArray<FSWGWaypointEntry> USWGWaypointSubsystem::GetWaypoints() const
{
	TArray<FSWGWaypointEntry> Result = CachedEntries;
	for (const FSWGWaypointEntry& MissionWaypoint : GetMissionWaypoints())
	{
		if (!Result.ContainsByPredicate([&MissionWaypoint](const FSWGWaypointEntry& Existing) { return Existing.WaypointObjectId == MissionWaypoint.WaypointObjectId; }))
		{
			Result.Add(MissionWaypoint);
		}
	}
	if (Result.IsEmpty())
	{
		return Result;
	}

	// Raw-space, matching CompassDirection/distance's frame — see USWGMissionSubsystem::GetMissions.
	const AActor* PlayerActor = ObjectGraph ? ObjectGraph->FindActor(ObjectGraph->GetLocalPlayerObjectId()) : nullptr;
	TOptional<FVector> PlayerRaw;
	if (PlayerActor)
	{
		PlayerRaw = SWGToRawSpace(PlayerActor->GetActorLocation());
	}

	for (FSWGWaypointEntry& Entry : Result)
	{
		if (PlayerRaw.IsSet())
		{
			// Nothing here checks PlanetCRC against the player's current planet yet —
			// same known gap as USWGMissionSubsystem::GetMissions.
			Entry.DistanceMeters = FVector::Dist2D(*PlayerRaw, Entry.RawPosition);
			Entry.Direction = CompassDirection(*PlayerRaw, Entry.RawPosition, Entry.BearingDegrees);
			Entry.bHasDistance = true;
		}

		Entry.bIsWorldLoaded = Entry.CellId == 0 && Terrain
			&& Terrain->IsPositionStreamed(FVector2D(Entry.RawPosition.X, Entry.RawPosition.Y));
	}
	return Result;
}

TArray<FSWGWaypointEntry> USWGWaypointSubsystem::GetActiveWaypoints() const
{
	TArray<FSWGWaypointEntry> Result = GetWaypoints();
	Result.RemoveAll([](const FSWGWaypointEntry& Entry) { return !Entry.bActive; });
	return Result;
}

FLinearColor USWGWaypointSubsystem::GetWaypointColor(ESWGWaypointColor Color)
{
	switch (Color)
	{
		case ESWGWaypointColor::Blue:   return FLinearColor::FromSRGBColor(FColor(0x4D, 0xA6, 0xFF));
		case ESWGWaypointColor::Green:  return FLinearColor::FromSRGBColor(FColor(0x37, 0xFD, 0x06));
		case ESWGWaypointColor::Orange: return FLinearColor::FromSRGBColor(FColor(0xFF, 0x8C, 0x1A));
		case ESWGWaypointColor::Yellow: return FLinearColor::FromSRGBColor(FColor(0xFF, 0xE8, 0x4D));
		case ESWGWaypointColor::Purple: return FLinearColor::FromSRGBColor(FColor(0xC8, 0x6D, 0xFF));
		case ESWGWaypointColor::White:
		default:                        return FLinearColor::White;
	}
}

void USWGWaypointSubsystem::UpdateMarkers(const TArray<FSWGWaypointEntry>& ActiveWaypoints)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const AActor* PlayerActor = ObjectGraph ? ObjectGraph->FindActor(ObjectGraph->GetLocalPlayerObjectId()) : nullptr;
	if (!World || !PlayerActor)
	{
		return;
	}
	const FVector PlayerLocation = PlayerActor->GetActorLocation();

	TSet<int64> Seen;
	Seen.Reserve(ActiveWaypoints.Num());

	for (const FSWGWaypointEntry& Entry : ActiveWaypoints)
	{
		if (!Entry.bIsWorldLoaded)
		{
			continue;
		}
		Seen.Add(Entry.WaypointObjectId);

		const FVector WorldLocation = SWGToUnrealSpace(Entry.RawPosition);

		TObjectPtr<ASWGWaypointMarker>* Existing = Markers.Find(Entry.WaypointObjectId);
		ASWGWaypointMarker* Marker = (Existing && IsValid(*Existing)) ? *Existing : nullptr;
		if (!Marker)
		{
			Marker = World->SpawnActor<ASWGWaypointMarker>(WorldLocation, FRotator::ZeroRotator);
			if (!Marker)
			{
				continue;
			}
			Markers.Add(Entry.WaypointObjectId, Marker);
			NextBreadcrumbRefreshTime.Remove(Entry.WaypointObjectId); // force an immediate breadcrumb pass below
		}
		else if (!Marker->GetActorLocation().Equals(WorldLocation, 1.f))
		{
			Marker->SetActorLocation(WorldLocation);
		}

		Marker->SetColor(GetWaypointColor(Entry.Color));

		const double Now = World->GetTimeSeconds();
		const double* NextRefresh = NextBreadcrumbRefreshTime.Find(Entry.WaypointObjectId);
		if (!NextRefresh || Now >= *NextRefresh)
		{
			RefreshBreadcrumbs(*Marker, PlayerLocation, WorldLocation);
			NextBreadcrumbRefreshTime.Add(Entry.WaypointObjectId, Now + BreadcrumbRefreshInterval);
		}
	}

	for (auto It = Markers.CreateIterator(); It; ++It)
	{
		if (Seen.Contains(It->Key))
		{
			continue;
		}
		if (IsValid(It->Value))
		{
			It->Value->Destroy();
		}
		NextBreadcrumbRefreshTime.Remove(It->Key);
		It.RemoveCurrent();
	}
}

void USWGWaypointSubsystem::UpdateCompassArrow(const TArray<FSWGWaypointEntry>& ActiveWaypoints)
{
	// Turned off for now — USWGWaypointMarkerWidget's screen-space label/edge-arrow
	// is considered enough on its own. Left in rather than deleted in case that
	// changes; flip this back on to re-enable the player-following ground arrow.
	if (!bCompassArrowEnabled)
	{
		if (IsValid(CompassArrow))
		{
			CompassArrow->Destroy();
		}
		CompassArrow = nullptr;
		return;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	AActor* PlayerActor = ObjectGraph ? ObjectGraph->FindActor(ObjectGraph->GetLocalPlayerObjectId()) : nullptr;

	const FSWGWaypointEntry* Nearest = nullptr;
	for (const FSWGWaypointEntry& Entry : ActiveWaypoints)
	{
		if (!Entry.bHasDistance)
		{
			continue;
		}
		if (!Nearest || Entry.DistanceMeters < Nearest->DistanceMeters)
		{
			Nearest = &Entry;
		}
	}

	if (!World || !PlayerActor || !Nearest)
	{
		if (IsValid(CompassArrow))
		{
			CompassArrow->Destroy();
		}
		CompassArrow = nullptr;
		return;
	}

	if (!IsValid(CompassArrow))
	{
		CompassArrow = World->SpawnActor<ASWGWaypointCompassArrow>();
		UE_LOG(LogSWGWaypoint, Log, TEXT("UpdateCompassArrow: spawned=%s nearest='%s' dist=%.0fm bearing=%.0f"),
			CompassArrow ? TEXT("yes") : TEXT("NO — SpawnActor failed"), *Nearest->Name.ToString(), Nearest->DistanceMeters, Nearest->BearingDegrees);
	}
	if (!CompassArrow)
	{
		return;
	}

	// Characters report their capsule center, not their feet.
	FVector FootLocation = PlayerActor->GetActorLocation();
	if (const ACharacter* Character = Cast<ACharacter>(PlayerActor))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			FootLocation.Z -= Capsule->GetScaledCapsuleHalfHeight();
		}
	}
	FootLocation.Z += CompassArrowGroundOffset;

	CompassArrow->SetActorLocation(FootLocation);
	CompassArrow->SetHeadingDegrees(Nearest->BearingDegrees);
	CompassArrow->SetColor(GetWaypointColor(Nearest->Color));
}

void USWGWaypointSubsystem::RefreshBreadcrumbs(ASWGWaypointMarker& Marker, const FVector& PlayerLocation, const FVector& WaypointLocation) const
{
	UWorld* World = Marker.GetWorld();
	UNavigationSystemV1* NavSystem = World ? UNavigationSystemV1::GetCurrent(World) : nullptr;

	if (NavSystem)
	{
		if (UNavigationPath* Path = NavSystem->FindPathToLocationSynchronously(World, PlayerLocation, WaypointLocation))
		{
			if (Path->IsValid() && Path->PathPoints.Num() > 1)
			{
				// PathPoints[0] is the player's own start point — an arrow right at
				// their feet adds nothing. Each arrow faces the next point along the
				// route; the last one faces the waypoint itself.
				TArray<FTransform> Trail;
				Trail.Reserve(Path->PathPoints.Num() - 1);
				for (int32 Index = 1; Index < Path->PathPoints.Num(); ++Index)
				{
					const FVector& Point = Path->PathPoints[Index];
					const FVector& Next = (Index + 1 < Path->PathPoints.Num()) ? Path->PathPoints[Index + 1] : WaypointLocation;
					const FRotator Facing = (Next - Point).GetSafeNormal().Rotation();
					Trail.Add(FTransform(Facing, Point));
				}
				Marker.SetBreadcrumbPoints(Trail);
				return;
			}
		}
	}

	// No navmesh covers this yet (or it's still generating) — a single arrow a
	// short distance ahead is a better hint than nothing, but implying a
	// walkable route all the way to a waypoint that might be kilometers away,
	// across water or terrain the player hasn't even seen yet, would be
	// actively misleading — so just the one, not a fake trail.
	const FVector ToWaypoint = WaypointLocation - PlayerLocation;
	const float Distance = FMath::Min(ToWaypoint.Size(), BreadcrumbFallbackDistance);
	if (Distance < BreadcrumbSpacing)
	{
		Marker.SetBreadcrumbPoints(TArray<FTransform>());
		return;
	}

	const FVector Direction = ToWaypoint.GetSafeNormal();
	const FTransform ArrowTransform(Direction.Rotation(), PlayerLocation + Direction * Distance);
	Marker.SetBreadcrumbPoints({ ArrowTransform });
}
