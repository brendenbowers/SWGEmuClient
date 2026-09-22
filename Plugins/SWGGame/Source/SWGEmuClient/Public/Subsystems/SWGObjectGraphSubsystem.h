

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
class ASWGCell;
class ASWGCreature;
struct FSWGNetMessage;
struct FSceneCreateObjectMessage;
struct FBaselinesMessage;
struct FSceneEndBaselinesMessage;
struct FSceneDestroyObjectMessage;
enum class ESWGObjectType : uint32;
struct FDeltasMessage;
struct FCmdStartSceneMessage;
struct FUpdateContainmentMessage;
struct FUpdateTransformMessage;
struct FUpdateTransformWithParentMessage;
struct FObjControllerMessageIn;
class USWGTerrainSubsystem;

/**
 * Owns the live object graph: the CRC->actor-class dispatch table (built once
 * from USWGTreSubsystem + the FormTag->ActorClass DataTable), the
 * ObjectId->Actor registry, and the SceneCreateObjectByCrc/BaselinesMessage/
 * SceneEndBaselines/DeltasMessage handling described in
 * world-object-plan.html ("Object graph subsystem — session lifecycle").
 */
UCLASS()
class SWGEMUCLIENT_API USWGObjectGraphSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
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

	/**
	 * The template CRC SceneCreateObjectByCrc reported for ObjectId, or 0 if
	 * none has arrived. Recorded even for objects that resolve to no actor
	 * class (e.g. ITNO/intangible — see HandleSceneCreateObject) since that's
	 * otherwise the only place this CRC is ever seen client-side, and
	 * USWGItemIconSubsystem needs it to preview an item with no spawned actor.
	 */
	uint32 FindObjectCrc(int64 ObjectId) const { return ObjectCrcById.FindRef(ObjectId); }

	template<typename T>
	T* FindComponent(int64 ObjectId) const
	{
		AActor* Actor = FindActor(ObjectId);
		return Actor ? Actor->FindComponentByClass<T>() : nullptr;
	}

	/** ObjectId's most recent UpdateContainmentMessage::ContainerId, or null if none has arrived yet. */
	const int64* FindContainerId(int64 ObjectId) const { return ContainerByObjectId.Find(ObjectId); }

	/** ObjectId's most recent containmentType (see ESWGContainmentType), or null if none has arrived yet. */
	const int32* FindContainmentType(int64 ObjectId) const { return ContainmentTypeByObjectId.Find(ObjectId); }

	/** Every object whose latest containment puts it directly inside ContainerId — a creature's gear and bags, a bag's contents. */
	TArray<int64> FindContainedObjectIds(int64 ContainerId) const;

	/**
	 * ObjectId's cellNumber, or null
	 */
	const int32* FindCellNumber(int64 ObjectId) const { return CellNumberByObjectId.Find(ObjectId); }

	/** Records ObjectId's cellNumber — set by FSWGCellBaselineHandler from the TLCS baseline. */
	void SetCellNumber(int64 ObjectId, int32 CellNumber) { CellNumberByObjectId.Add(ObjectId, CellNumber); }

	/**
	 * Registers a client-known static object (a world-snapshot building, cell
	 * or prop) under its .ws ObjectID. The server creates these as client
	 * objects with that same id and never sends SceneCreateObjectByCrc for
	 * them, but NPC containment and targeting still reference it. Registering
	 * a cell also re-applies containment for anything that arrived into it
	 * before the terrain/snapshot load finished.
	 */
	void RegisterStaticObject(int64 ObjectId, AActor* Actor, int64 ContainerId = 0);

	/** Forgets a static object's registry entries without touching its actor — ASWGBuilding::UnloadRoom destroys that itself. */
	void UnregisterStaticObject(int64 ObjectId);

	/**
	 * A network cell just got its building (FSWGCellSpawnHandler::FinishCell):
	 * place whatever zoned in inside it and has been waiting for a real
	 * transform to compose against.
	 */
	void NotifyCellFinished(int64 CellObjectId);

	/** Whether the local player's server-side container is ContainerId (a cell, usually). */
	bool IsLocalPlayerContainedIn(int64 ContainerId) const;

	/**
	 * Whether ObjectId is somewhere in the local player's own possessions —
	 * equipped, in the inventory bag, the datapad, nested arbitrarily deep —
	 * by walking FindContainerId up to LocalPlayerObjectId. False for world
	 * objects, other players/NPCs, and anything whose chain doesn't resolve
	 * (capped hop count against bad/cyclic containment data).
	 */
	bool IsOwnedByLocalPlayer(int64 ObjectId) const;

	/** Fired once SceneEndBaselines confirms an object's baselines are complete. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnObjectReady, int64 /*ObjectId*/);
	FOnObjectReady OnObjectReady;

	/** Fired just before a destroyed object's actor is torn down, while it can still be looked up. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnObjectDestroyed, int64 /*ObjectId*/);
	FOnObjectDestroyed OnObjectDestroyed;

	/**
	 * Destroys ObjectId's actor and drops every trace of it from the graph.
	 * Safe to call for an id that isn't registered.
	 */
	void RemoveObject(int64 ObjectId);

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
	 * new zone level. UGameplayStatics::OpenLevel is deferred to the next world
	 * travel tick, so messages arriving between CmdStartScene and the actual
	 * level swap would otherwise spawn into the old, about-to-be-destroyed
	 * level. Replays every message queued during that window in order.
	 */
	void OnZoneLevelLoaded();

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg);

	void HandleCmdStartScene(const FCmdStartSceneMessage& Msg);
	void HandleSceneCreateObject(const FSceneCreateObjectMessage& Msg);
	void HandleBaselines(const FBaselinesMessage& Msg);
	void HandleSceneEndBaselines(const FSceneEndBaselinesMessage& Msg);
	void HandleSceneDestroyObject(const FSceneDestroyObjectMessage& Msg);

	/** The actor a baseline/delta applies to. Most types own their actor; the player object doesn't. */
	AActor* ResolveMessageActor(int64 ObjectId, ESWGObjectType ObjectType);

	/** Resolves the player object to the creature holding it, registering it on first sight. */
	AActor* ResolvePlayerObjectActor(int64 ObjectId);
	void HandleDeltas(const FDeltasMessage& Msg);
	void HandleUpdateContainment(const FUpdateContainmentMessage& Msg);
	void HandleUpdateTransform(const FUpdateTransformMessage& Msg);
	void HandleUpdateTransformWithParent(const FUpdateTransformWithParentMessage& Msg);
	/** Smoothed (or immediate for teleports and the local pawn) move to a world-space destination — shared by both transform messages. */
	void ApplyNetworkTransform(AActor* Actor, int64 ObjectId, const FVector& NewLocation, float YawDegrees);
	void HandleObjControllerMessage(const FObjControllerMessageIn& Msg);

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

	/**
	 * Hides/shows Actor for a container change, or no-ops if Actor is null
	 * (containment can arrive before the actor's SceneCreateObjectByCrc).
	 * ObjectId is Actor's own object id — used to look up
	 * ContainmentTypeByObjectId so a Rider containment (mounting) can be told
	 * apart from ordinary volume containment (hidden away in a bag).
	 */
	void ApplyContainment(AActor* Actor, int64 ObjectId, int64 ContainerId);

	/** Rider-containment half of ApplyContainment: attach/detach Actor to/from a mount's rider hardpoint instead of hiding it. */
	void ApplyRiderContainment(ASWGCreature* Rider, ASWGCreature* Mount);

	/**
	 * Hands a slotted item to its container creature's USWGEquipmentComponent,
	 * and takes it back from the previous one. Core3 only fills CREO6's
	 * wearables list for players (PlayerContainerComponent); an NPC's outfit,
	 * weapon and hair arrive solely as child TANOs with a slotted
	 * UpdateContainmentMessage, which is what the retail client renders from.
	 * Needs the item's TANO3 (customization), so callers run it at
	 * SceneEndBaselines, or on a containment change after that.
	 */
	void SyncSlottedEquipment(int64 ObjectId, int64 PreviousContainerId);

	/**
	 * Applies a creature template's baked-in .cdf appearance — body
	 * customization onto the tangible component and FORM WEAR outfit onto
	 * the equipment component. Must run before the body mesh request so the
	 * customization is in place when that mesh builds.
	 */
	void ApplyClientDataFile(AActor* Actor, uint32 TemplateCrc);

	TMap<uint32, TSubclassOf<AActor>> CrcToActorClass;
	bool bCrcMapBuilt = false;

	TMap<int64, TWeakObjectPtr<AActor>> ActorRegistry;

	/** ObjectId -> template CRC, recorded for every SceneCreateObjectByCrc regardless of whether an actor spawned — see FindObjectCrc. */
	TMap<int64, uint32> ObjectCrcById;

	/** Above this horizontal jump an update is applied directly instead of walked to. Sized so a sprint between sparse updates still smooths, but a zone-in doesn't get strolled to. */
	static constexpr float MaxSmoothedMoveDistance = 1500.0f;

	/** ObjectId -> ContainerId (0 = no container / placed in the world) from the most recent UpdateContainmentMessage — checked by
	 *  HandleSceneEndBaselines so a contained object doesn't get revealed as a free-floating world actor at its raw (usually (0,0,0)) position. */
	TMap<int64, int64> ContainerByObjectId;

	/** ObjectId -> containmentType from the same message (see ESWGContainmentType); slotted values mean "equipped". */
	TMap<int64, int32> ContainmentTypeByObjectId;

	/** Objects whose SceneEndBaselines has been seen — a later containment change for one of these is applied immediately. */
	TSet<int64> ReadyObjects;

	/** ObjectId -> cellNumber */
	TMap<int64, int32> CellNumberByObjectId;

	UPROPERTY()
	TObjectPtr<USWGTerrainSubsystem> TerrainSubsystem;

	int64 LocalPlayerObjectId = 0;

	/** The local player finished baselines inside a not-yet-loaded cell; RevealCurrentZoneLevel waits for its placement. */
	bool bRevealPendingPlayerPlacement = false;

	/** The PLAY object's id once seen — shares the local player's actor, so its destroy must not tear that down. */
	int64 PlayerObjectId = 0;

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
	TObjectPtr<USWGTreSubsystem> TreSubsystem;

	FDelegateHandle MessageHandle;
};
