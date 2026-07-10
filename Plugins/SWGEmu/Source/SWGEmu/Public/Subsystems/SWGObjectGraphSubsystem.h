

#pragma once


#include "CoreMinimal.h"
#include "Async/Async.h"
#include "Async/Future.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "SWGObjectGraphSubsystem.generated.h"

class USWGTreSubsystem;
class USWGNetworkSubsystem;
class USWGMeshGeneratorSubsystem;
class UDataTable;
class ULevelStreaming;
struct FSWGNetMessage;
struct FSceneCreateObjectMessage;
struct FBaselinesMessage;
struct FSceneEndBaselinesMessage;
struct FDeltasMessage;
struct FCmdStartSceneMessage;
struct FUpdateContainmentMessage;
struct FUpdateTransformMessage;
class USWGTerrainSubsystem;

/**
 * Owns the live object graph: the CRC->actor-class dispatch table (built once
 * from USWGTreSubsystem + the FormTag->ActorClass DataTable), the
 * ObjectId->Actor registry, and the SceneCreateObjectByCrc/BaselinesMessage/
 * SceneEndBaselines/DeltasMessage handling described in
 * world-object-plan.html ("Object graph subsystem — session lifecycle").
 */
UCLASS()
class SWGEMU_API USWGObjectGraphSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	USWGObjectGraphSubsystem() = default;

	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	// ── CRC -> actor class ─────────────────────────────────────────


	/**
	 * Adopts an already-built CRC->actor-class map (e.g. one built inline by
	 * FSWGInitializationState today). Temporary home for this until CRC
	 * resolution moves to a proper load-action/task of its own.
	 */
	void SetCrcToActorClassMap(TMap<uint32, TSubclassOf<AActor>> InMap);

	/** Looks up the actor class for a CRC (from SceneCreateObjectByCrc). Null if unresolved or "don't spawn." */
	TSubclassOf<AActor> ResolveActorClassForCrc(uint32 Crc) const;

	bool IsCrcMapBuilt() const { return bCrcMapBuilt; }
	int32 GetResolvedCrcCount() const { return CrcToActorClass.Num(); }

	// ── ObjectId -> Actor registry ──────────────────────────────────

	AActor* FindActor(int64 ObjectId) const;

	template<typename T>
	T* FindComponent(int64 ObjectId) const
	{
		AActor* Actor = FindActor(ObjectId);
		return Actor ? Actor->FindComponentByClass<T>() : nullptr;
	}

	/** Fired once SceneEndBaselines confirms an object's baselines are complete. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnObjectReady, int64 /*ObjectId*/);
	FOnObjectReady OnObjectReady;

	/**
	 * CharacterID from CmdStartScene — the ObjectId of the local player's own
	 * CREO (same value used server-side as the character's object id). 0 until
	 * CmdStartScene arrives for the current zone session.
	 */
	int64 GetLocalPlayerObjectId() const { return LocalPlayerObjectId; }

	// ── Zone level targeting ────────────────────────────────────────

	/**
	 * Sets which streaming level newly spawned world objects belong to
	 * (via FActorSpawnParameters::OverrideLevel). Pass nullptr to fall back
	 * to default placement (PersistentLevel). Spawning every object-graph
	 * actor into the zone's own streaming level — rather than the persistent
	 * level — means unloading that level on zone exit destroys all of them
	 * automatically, without USWGObjectGraphSubsystem needing to manually
	 * walk ActorRegistry and destroy each one.
	 */
	void SetCurrentZoneLevel(ULevelStreaming* Streaming);

	/** Resolves the actual ULevel new actors should spawn into right now, or nullptr if none set / not yet loaded. */
	ULevel* GetSpawnLevel() const;

	/**
	 * Flips the current zone's streaming level visible. Called automatically
	 * once the local player's own CREO finishes its baselines (see
	 * HandleSceneEndBaselines) — there's no "all baselines received" moment
	 * for the zone as a whole (objects keep streaming in as you move around),
	 * so the player's own readiness is the trigger, matching retail client
	 * behavior.
	 */
	void RevealCurrentZoneLevel();

	/** Fired the moment RevealCurrentZoneLevel() actually flips visibility (e.g. to dismiss a loading widget). */
	DECLARE_MULTICAST_DELEGATE(FOnZoneLevelRevealed);
	FOnZoneLevelRevealed OnZoneLevelRevealed;

	/**
	 * Called from FSWGZoneLoadingState once PostLoadMapWithWorld fires for the
	 * new zone level — mirrors the guard USWGTerrainSubsystem::BeginLoadTerrain
	 * already needed for the same reason (see FSWGZoneLoadingState::Enter's
	 * comment): UGameplayStatics::OpenLevel is deferred to the next world
	 * travel tick, not immediate, so every SceneCreateObjectByCrc/Baselines/
	 * SceneEndBaselines message arriving between CmdStartScene and the actual
	 * level swap — including the local player's own spawn, sent exactly once
	 * — was being spawned into the OLD, about-to-be-destroyed level and
	 * silently destroyed the instant OpenLevel took effect (confirmed via a
	 * dedicated ASWGPlayer::EndPlay log: reason=LevelTransition, ~0.1s after
	 * the player's own SceneEndBaselines "revealed" line, every single zone
	 * load). Replays every message queued during that window in order.
	 */
	void OnZoneLevelLoaded();

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg);

	void HandleCmdStartScene(const FCmdStartSceneMessage& Msg);
	void HandleSceneCreateObject(const FSceneCreateObjectMessage& Msg);
	void HandleBaselines(const FBaselinesMessage& Msg);
	void HandleSceneEndBaselines(const FSceneEndBaselinesMessage& Msg);
	void HandleDeltas(const FDeltasMessage& Msg);
	void HandleUpdateContainment(const FUpdateContainmentMessage& Msg);
	void HandleUpdateTransform(const FUpdateTransformMessage& Msg);

	/**
	 * The network sends feet/ground-level Z, but AActor::SetActorLocation
	 * places the actor origin there — which for an ACharacter is the capsule's
	 * *center*, not its bottom (confirmed: the default capsule half-height
	 * offset some engines bake into a character's mesh doesn't exist in the
	 * base C++ ACharacter, only in the Blueprint third-person template — so
	 * nothing was correcting for this). Without this, every creature/player
	 * renders floating at capsule-half-height above the ground. Returns
	 * NetworkPos unchanged for non-ACharacter actors (items sit directly on
	 * their own reported position, no capsule to account for).
	 */
	static FVector GroundedLocationFor(const AActor* Actor, const FVector& NetworkPos);

	/** Hides/shows Actor for a container change, or no-ops if Actor is null (containment can arrive before the actor's SceneCreateObjectByCrc). */
	void ApplyContainment(AActor* Actor, int64 ContainerId);

	TMap<uint32, TSubclassOf<AActor>> CrcToActorClass;
	bool bCrcMapBuilt = false;

	TMap<int64, TWeakObjectPtr<AActor>> ActorRegistry;

	/** ObjectId -> ContainerId (0 = no container / placed in the world) from the most recent UpdateContainmentMessage — checked by
	 *  HandleSceneEndBaselines so a contained object doesn't get revealed as a free-floating world actor at its raw (usually (0,0,0)) position. */
	TMap<int64, int64> ContainerByObjectId;

	UPROPERTY()
	TObjectPtr<USWGTerrainSubsystem> TerrainSubsystem;

	int64 LocalPlayerObjectId = 0;

	/**
	 * False from the moment a CmdStartScene starts a new zone load until
	 * OnZoneLevelLoaded() fires — see that function's comment. Every
	 * non-CmdStartScene message received while false is buffered in
	 * PendingMessages instead of processed immediately.
	 */
	bool bLevelReadyForObjects = true;
	TArray<TSharedPtr<FSWGNetMessage>> PendingMessages;

	TWeakObjectPtr<ULevelStreaming> CurrentZoneStreamingLevel;

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;
	TObjectPtr<USWGMeshGeneratorSubsystem> MeshGenerator;

	FDelegateHandle MessageHandle;
};
