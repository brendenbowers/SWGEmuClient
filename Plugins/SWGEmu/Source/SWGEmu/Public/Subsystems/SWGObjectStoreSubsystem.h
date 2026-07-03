#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/Objects/Zone/Object/TangibleObjectBaseline.h"
#include "Network/Objects/Zone/Creature/CreatureObjectBaseline.h"
#include "Network/Objects/Zone/Player/PlayerObjectBaseline.h"
#include "SWGObjectStoreSubsystem.generated.h"

class USWGNetworkSubsystem;
struct FSWGNetMessage;

/**
 * Decoded, accumulated state for one zone object across its lifetime:
 * SceneCreateObjectByCrc (spawn transform/template) → one or more
 * BaselinesMessage slots (full snapshot per object type) → SceneEndBaselines
 * (ready). DeltasMessage updates are folded into the same sub-object structs
 * as they arrive.
 *
 * Only the sub-object matching the wire ObjectType FourCC is populated —
 * e.g. a plain TANO item never gets a Creature or Player struct.
 */
struct SWGEMU_API FSWGObjectRecord
{
	int64  ObjectId     = 0;
	uint32 ObjectCrc    = 0; // Template CRC from SceneCreateObjectByCrc
	float  PosX = 0.f, PosY = 0.f, PosZ = 0.f;
	float  DirX = 0.f, DirY = 0.f, DirZ = 0.f, DirW = 0.f;
	bool   bHyperspacing = false;
	bool   bReady         = false; // Set once SceneEndBaselines arrives

	TOptional<FTangibleObjectBaseline> Tangible;
	TOptional<FCreatureObjectBaseline> Creature;
	TOptional<FPlayerObjectBaseline>   Player;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnSWGObjectReady, int64 /*ObjectId*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSWGObjectDestroyed, int64 /*ObjectId*/);

/**
 * USWGObjectStoreSubsystem — accumulates per-object baseline/delta state.
 *
 * Decoupled from USWGNetworkSubsystem the same way USWGMessageWaitSubsystem
 * is: subscribes to OnMessageReceived and reacts to the zone object lifecycle
 * messages (SceneCreateObjectByCrc, BaselinesMessage, SceneEndBaselines,
 * DeltasMessage) without the transport layer knowing this subsystem exists.
 */
UCLASS()
class SWGEMU_API USWGObjectStoreSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Returns the accumulated record for an object, or null if unknown. */
	const FSWGObjectRecord* FindObject(int64 ObjectId) const;

	/** Fired once SceneEndBaselines confirms an object's baselines are complete. */
	FOnSWGObjectReady OnObjectReady;

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg);

	FSWGObjectRecord& FindOrAdd(int64 ObjectId);

	TMap<int64, TSharedPtr<FSWGObjectRecord>> Objects;

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network = nullptr;

	FDelegateHandle MessageHandle;
};
