#include "Subsystems/SWGInteriorStreamingSubsystem.h"
#include "Objects/World/SWGBuilding.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// All distances in SWG metres (raw units). Negative disables that tier.
	TAutoConsoleVariable<float> CVarInteriorVisibleRadius(
		TEXT("swg.InteriorVisibleRadius"), 128.0f,
		TEXT("Distance within which a building's exterior-visible rooms are built when the camera is looking at it."));

	TAutoConsoleVariable<float> CVarInteriorLoadRadius(
		TEXT("swg.InteriorLoadRadius"), 48.0f,
		TEXT("Distance within which a building's closed rooms are built (no view test)."));

	// Wider than half the FOV so a building just off-screen is ready when the
	// camera pans onto it, and so the offset between the building's origin
	// and its doorway doesn't drop it out of the cone.
	TAutoConsoleVariable<float> CVarInteriorViewHalfAngle(
		TEXT("swg.InteriorViewHalfAngle"), 70.0f,
		TEXT("Half-angle (degrees) of the view cone used for the exterior-visible room tier."));
}

TStatId USWGInteriorStreamingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGInteriorStreamingSubsystem, STATGROUP_Tickables);
}

void USWGInteriorStreamingSubsystem::RegisterBuilding(ASWGBuilding* Building)
{
	if (Building)
	{
		Buildings.AddUnique(Building);
	}
}

USWGInteriorStreamingSubsystem::FViewState USWGInteriorStreamingSubsystem::GetViewState() const
{
	FViewState View;

	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->GetPawn())
	{
		return View;
	}
	View.PawnLocation = PC->GetPawn()->GetActorLocation();

	// The camera, not the pawn: in third person the pawn faces where it walks,
	// the camera is what's looking at the doorway.
	if (const APlayerCameraManager* Camera = PC->PlayerCameraManager)
	{
		View.CameraLocation = Camera->GetCameraLocation();
		View.CameraForward = Camera->GetCameraRotation().Vector();
	}
	else
	{
		View.CameraLocation = PC->GetPawn()->GetActorLocation();
		View.CameraForward = PC->GetPawn()->GetActorForwardVector();
	}

	if (const USWGObjectGraphSubsystem* ObjectGraph = GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>())
	{
		if (const int64* ContainerId = ObjectGraph->FindContainerId(ObjectGraph->GetLocalPlayerObjectId()))
		{
			View.PlayerContainerId = *ContainerId;
		}
	}

	View.VisibleDistance = SWGToUnrealSpace(CVarInteriorVisibleRadius.GetValueOnGameThread());
	View.ClosedDistance = SWGToUnrealSpace(CVarInteriorLoadRadius.GetValueOnGameThread());
	View.CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(CVarInteriorViewHalfAngle.GetValueOnGameThread()));
	View.bValid = true;
	return View;
}

float USWGInteriorStreamingSubsystem::DistanceToBuilding(const FViewState& View, const ASWGBuilding& Building)
{
	const FBox2D Footprint = Building.GetFootprint();
	const FVector2D Camera2D(View.CameraLocation.X, View.CameraLocation.Y);
	return Footprint.bIsValid ? FMath::Sqrt(Footprint.ComputeSquaredDistanceToPoint(Camera2D)) : FVector::Dist2D(View.CameraLocation, Building.GetActorLocation());
}

bool USWGInteriorStreamingSubsystem::WantsRoom(const FViewState& View, ASWGBuilding& Building, int32 CellIndex, bool bForUnload)
{
	if (!View.bValid)
	{
		return false;
	}

	// The server's containment is the only signal on zone-in inside a
	// building: no triggers exist yet and the player's position is still
	// cell-relative, so no distance test would pass.
	if (Building.IsPlayerInside() || Building.OwnsCell(View.PlayerContainerId))
	{
		return true;
	}

	// A closed room is only ever seen from inside, so it loads when the player
	// is close in front of any doorway; an exterior-visible room shows through
	// its own doorway from further off, but only while that doorway is looked at.
	const bool bClosed = Building.IsClosedRoom(CellIndex);
	const float Range = (bClosed ? View.ClosedDistance : View.VisibleDistance) * (bForUnload ? UnloadHysteresis : 1.0f);
	const float MinFacingDot = bForUnload ? EntranceFacingUnloadDot : EntranceFacingLoadDot;
	const bool bRequireInViewCone = !bClosed && !bForUnload;
	if (Range < 0.0f)
	{
		return false;
	}

	const FVector Forward2D = FVector(View.CameraForward.X, View.CameraForward.Y, 0.0f).GetSafeNormal();
	const FTransform& BuildingTransform = Building.GetActorTransform();
	const TArray<ASWGBuilding::FEntrance>& Entrances = Building.GetEntrances();

	// No doorway geometry in the POB: fall back to the building as a whole.
	if (Entrances.IsEmpty())
	{
		if (DistanceToBuilding(View, Building) > Range)
		{
			return false;
		}
		const FVector ToBuilding2D = FVector(Building.GetActorLocation() - View.CameraLocation).GetSafeNormal2D();
		return !bRequireInViewCone || FVector::DotProduct(Forward2D, ToBuilding2D) >= View.CosHalfAngle;
	}

	for (const ASWGBuilding::FEntrance& Entrance : Entrances)
	{
		if (!bClosed && Entrance.CellIndex != CellIndex)
		{
			continue;
		}

		const FVector Center = BuildingTransform.TransformPosition(Entrance.Center);
		const FVector Outward2D = BuildingTransform.TransformVectorNoScale(Entrance.Normal).GetSafeNormal2D();
		const FVector ToCamera2D = (View.CameraLocation - Center).GetSafeNormal2D();

		if (FVector::DotProduct(Outward2D, ToCamera2D) < MinFacingDot)
		{
			continue;
		}
		if (FVector::Dist2D(View.CameraLocation, Center) > Range)
		{
			continue;
		}
		if (bRequireInViewCone && FVector::DotProduct(Forward2D, -ToCamera2D) < View.CosHalfAngle)
		{
			continue;
		}
		return true;
	}
	return false;
}

bool USWGInteriorStreamingSubsystem::ShouldLoadRoom(ASWGBuilding& Building, int32 CellIndex) const
{
	return WantsRoom(GetViewState(), Building, CellIndex, /*bForUnload*/ false);
}

void USWGInteriorStreamingSubsystem::Tick(float DeltaTime)
{
	TimeUntilNextSweep -= DeltaTime;
	if (TimeUntilNextSweep > 0.0f)
	{
		return;
	}
	TimeUntilNextSweep = SweepInterval;

	const FViewState View = GetViewState();
	if (!View.bValid)
	{
		return;
	}

	for (auto It = Buildings.CreateIterator(); It; ++It)
	{
		ASWGBuilding* Building = It->Get();
		if (!Building)
		{
			It.RemoveCurrent();
			continue;
		}

		// Tested on the pawn, not the camera, which swings outside the walls
		// in third person. The server's containment counts as inside too.
		const FBox2D Footprint = Building->GetFootprint();
		const bool bWithinFootprint = Footprint.bIsValid && Footprint.IsInside(FVector2D(View.PawnLocation.X, View.PawnLocation.Y));
		Building->SetPlayerWithinFootprint(bWithinFootprint || Building->OwnsCell(View.PlayerContainerId));

		for (int32 CellIndex = 1; CellIndex < Building->GetRoomCount(); ++CellIndex)
		{
			if (!Building->IsRoomLoaded(CellIndex))
			{
				if (Building->HasDeferredRoom(CellIndex) && WantsRoom(View, *Building, CellIndex, /*bForUnload*/ false))
				{
					Building->LoadRoom(CellIndex);
				}
			}
			else if (!WantsRoom(View, *Building, CellIndex, /*bForUnload*/ true))
			{
				Building->UnloadRoom(CellIndex);
			}
		}

		// A network building's rooms are finished once and then belong to the
		// server's own range culling.
		if (!Building->HasDeferredInterior())
		{
			It.RemoveCurrent();
		}
	}
}
