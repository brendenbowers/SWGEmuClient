

#pragma once


#include "CoreMinimal.h"
#include "Async/Async.h"
#include "Async/Future.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "SWGObjectGraphSubsystem.generated.h"

class USWGTreSubsystem;
class USWGNetworkSubsystem;
class UDataTable;
class ULevelStreaming;
struct FSWGNetMessage;
struct FSceneCreateObjectMessage;
struct FBaselinesMessage;
struct FSceneEndBaselinesMessage;
struct FDeltasMessage;
struct FCmdStartSceneMessage;

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

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg);

	void HandleCmdStartScene(const FCmdStartSceneMessage& Msg);
	void HandleSceneCreateObject(const FSceneCreateObjectMessage& Msg);
	void HandleBaselines(const FBaselinesMessage& Msg);
	void HandleSceneEndBaselines(const FSceneEndBaselinesMessage& Msg);
	void HandleDeltas(const FDeltasMessage& Msg);

	TMap<uint32, TSubclassOf<AActor>> CrcToActorClass;
	bool bCrcMapBuilt = false;

	TMap<int64, TWeakObjectPtr<AActor>> ActorRegistry;

	int64 LocalPlayerObjectId = 0;

	TWeakObjectPtr<ULevelStreaming> CurrentZoneStreamingLevel;

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	FDelegateHandle MessageHandle;
};
