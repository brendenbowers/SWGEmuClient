#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "SWGInteriorStreamingSubsystem.generated.h"

class ASWGBuilding;

/**
 * Streams building interiors around the local player. Every room is deferred
 * at spawn (ASWGBuilding::DeferredSnapshotCells / DeferredNetworkCells) and
 * built in one of two tiers:
 *
 *  - exterior-visible rooms (POB canSeeParentCell): within
 *    swg.InteriorVisibleRadius AND inside the camera's view cone
 *    (swg.InteriorViewHalfAngle) — what you could actually see through the doorway;
 *  - closed rooms: within swg.InteriorLoadRadius, no view test — you're about
 *    to walk in.
 *
 * A loaded tier is only unloaded by distance (radius * hysteresis), never by
 * turning away, so panning the camera can't thrash it. Network buildings only
 * ever load — the server already range-culls those with SceneDestroyObject.
 */
UCLASS()
class SWGEMUCLIENT_API USWGInteriorStreamingSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return true; }

	/** Buildings call this once they hold any deferred room. Idempotent. */
	void RegisterBuilding(ASWGBuilding* Building);

	/** The load decision for one tier of Building, evaluated against the camera right now. */
	bool ShouldLoadRooms(const ASWGBuilding& Building, bool bClosed) const;

private:
	struct FViewState
	{
		FVector CameraLocation = FVector::ZeroVector;
		FVector CameraForward = FVector::ForwardVector;
		FVector PawnLocation = FVector::ZeroVector;
		float VisibleDistance = 0.0f;
		float ClosedDistance = 0.0f;
		float CosHalfAngle = 1.0f;

		/** The local player's container when it stands in a cell — that building loads regardless of distance or view. */
		int64 PlayerContainerId = 0;

		bool bValid = false;
	};

	FViewState GetViewState() const;
	static bool WantsRooms(const FViewState& View, const ASWGBuilding& Building, bool bClosed);

	/** XY distance from the camera to the building's footprint — 0 inside it. Against the origin a starport's doorway is 50m+ away. */
	static float DistanceToBuilding(const FViewState& View, const ASWGBuilding& Building);

	TArray<TWeakObjectPtr<ASWGBuilding>> Buildings;

	/** Distance checks are cheap but there's no reason to run them every frame. */
	float TimeUntilNextSweep = 0.0f;
	static constexpr float SweepInterval = 0.5f;

	/** Unload past the tier's radius * this, so a player pacing at the boundary doesn't thrash. */
	static constexpr float UnloadHysteresis = 1.25f;
};
