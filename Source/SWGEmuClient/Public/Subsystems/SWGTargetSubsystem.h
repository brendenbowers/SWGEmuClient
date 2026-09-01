#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "SWGTargetSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGObjectGraphSubsystem;
class USWGMeshGeneratorSubsystem;
class UPrimitiveComponent;
class UMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSWGOnTargetChanged, int64, NewTargetId, AActor*, NewTargetActor);

/**
 * Owns "what the player has targeted".
 */
UCLASS()
class SWGEMUCLIENT_API USWGTargetSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/**
	 * Targets ObjectId and tells the server. Pass 0 to clear. Targeting the
	 * object that is already targeted is a no-op — SWG re-sends nothing on a
	 * redundant click and neither do we.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Targeting")
	void SetTarget(int64 ObjectId);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Targeting")
	void ClearTarget() { SetTarget(0); }

	/** Targets Actor if it is a network object, otherwise clears. Convenience for click/trace paths. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Targeting")
	void SetTargetActor(AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Targeting")
	int64 GetTargetId() const { return CurrentTargetId; }

	/** The targeted object's actor, or null if we have no target or it isn't spawned. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Targeting")
	AActor* GetTargetActor() const;

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Targeting")
	bool HasTarget() const { return CurrentTargetId != 0; }

	/** Fired on every change, from either side of the wire. NewTargetActor may be null for a live id whose actor hasn't spawned. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Targeting")
	FSWGOnTargetChanged OnTargetChanged;

	/** Stencil index the outline post-process material tests against. */
	static constexpr int32 TargetStencilValue = 1;

	/**
	 * The channel mouse picking traces on — "SWGSelect" in DefaultEngine.ini,
	 * which every primitive ignores unless MakeSelectable() opts it in.
	 *
	 */
	static constexpr ECollisionChannel SelectionChannel = ECC_GameTraceChannel1;

	/**
	 * True if Actor is something the player is allowed to click on — i.e. it
	 * carries a USWGTangibleComponent. That covers creatures, items,
	 * buildings and installations, and excludes cells, doors, static props
	 * and the terrain, which are scenery rather than objects.
	 */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Targeting")
	static bool IsSelectable(const AActor* Actor);

	/**
	 * Opts Component into the selection channel as a query-only blocker,
	 * leaving every other channel untouched so this can never affect
	 * movement, physics or camera collision. Safe to call more than once.
	 */
	static void MakeSelectable(UPrimitiveComponent* Component);

private:
	/** Adopts NewTargetId, moves the highlight, and broadcasts. No-op if unchanged. */
	void ApplyTarget(int64 NewTargetId);

	/** Turns the custom-depth outline on or off for every primitive under an object's actor. */
	void SetHighlightEnabled(int64 ObjectId, bool bEnabled);

	void HandleObjectDestroyed(int64 ObjectId);

	/**
	 * Makes each newly generated mesh clickable. Meshes are built
	 * asynchronously and can land seconds after the actor spawns, so this
	 * rides USWGMeshGeneratorSubsystem::OnMeshReady rather than trying to
	 * configure collision at spawn time, when there is no geometry yet.
	 */
	void HandleMeshReady(AActor* Actor, UMeshComponent* MeshComponent);

	/** Seeds from, and watches for changes in, the local player's CREO6 target field. */
	void ReconcileWithServerTarget();

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGMeshGeneratorSubsystem> MeshGenerator;

	int64 CurrentTargetId = 0;

	int64 LastServerTargetId = 0;

	bool bSeenServerTarget = false;

	int64 SeenForPlayerObjectId = 0;

	static constexpr float ReconcileInterval = 0.1f;
	float TimeSinceReconcile = 0.f;

	FDelegateHandle ObjectDestroyedHandle;
	FDelegateHandle MeshReadyHandle;
};
