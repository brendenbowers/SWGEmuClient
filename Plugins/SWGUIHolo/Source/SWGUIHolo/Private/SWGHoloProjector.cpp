#include "SWGHoloProjector.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

bool SWGHoloProjector::FindLocation(APawn* Pawn, float Distance, float Height, FVector& OutLocation, float Side)
{
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	const FRotator Facing(0.f, Pawn->GetActorRotation().Yaw, 0.f);
	// The pawn's own origin isn't a ground reference; trace for the ground instead.
	OutLocation = Pawn->GetActorLocation() + Facing.RotateVector(FVector(Distance, Side, 0.f));
	FHitResult Ground;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SWGHoloGround), false, Pawn);
	if (World->LineTraceSingleByChannel(Ground, OutLocation + FVector(0.f, 0.f, 200.f), OutLocation - FVector(0.f, 0.f, 500.f), ECC_Visibility, Query))
	{
		OutLocation.Z = Ground.ImpactPoint.Z;
	}
	OutLocation.Z += Height;
	return true;
}
