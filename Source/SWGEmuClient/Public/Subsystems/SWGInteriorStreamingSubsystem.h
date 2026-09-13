#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "SWGInteriorStreamingSubsystem.generated.h"

class ASWGBuilding;

/**
 * Streams building interiors around the local player, room by room. Every
 * room is deferred at spawn (ASWGBuilding::DeferredSnapshotCells /
 * DeferredNetworkCells) and loaded against the building's doorways
 * (ASWGBuilding::GetEntrances):
 *
 *  - an exterior-visible room (POB canSeeParentCell) loads when the camera is
 *    in front of that room's own doorway, within swg.InteriorVisibleRadius of
 *    it, and looking at it (swg.InteriorViewHalfAngle);
 *  - a closed room loads when the camera is in front of any doorway within
 *    swg.InteriorLoadRadius — about to walk in — with no view test.
 *
 * Unloading needs the camera clearly behind the doorway or well beyond the
 * radius (hysteresis on both), never just turning away, so panning can't
 * thrash. Network buildings only ever load — the server already range-culls
 * those with SceneDestroyObject.
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

	/** The load decision for one room of Building, evaluated against the camera right now. */
	bool ShouldLoadRoom(ASWGBuilding& Building, int32 CellIndex) const;

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

	/** The load decision for one room; bForUnload evaluates the looser keep-loaded criteria instead. */
	static bool WantsRoom(const FViewState& View, ASWGBuilding& Building, int32 CellIndex, bool bForUnload);

	/** XY distance from the camera to the building's footprint — 0 inside it. Only used for buildings without doorway geometry. */
	static float DistanceToBuilding(const FViewState& View, const ASWGBuilding& Building);

	/** Load once clearly in front of a doorway; unload only once clearly behind — the gap stops thrash along its wall. */
	static constexpr float EntranceFacingLoadDot = 0.1f;
	static constexpr float EntranceFacingUnloadDot = -0.2f;

	TArray<TWeakObjectPtr<ASWGBuilding>> Buildings;

	/** Distance checks are cheap but there's no reason to run them every frame. */
	float TimeUntilNextSweep = 0.0f;
	static constexpr float SweepInterval = 0.5f;

	/** Unload past the tier's radius * this, so a player pacing at the boundary doesn't thrash. */
	static constexpr float UnloadHysteresis = 1.25f;
};
