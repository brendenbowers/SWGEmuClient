#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "SWGObject.generated.h"

/**
 * Root actor for the non-pawn branch of the world-object hierarchy
 * (ASWGItem, ASWGStaticProp, ASWGBuilding, ASWGCell, ASWGInstallation).
 * ASWGCreature does NOT derive from this — it's an ACharacter, and UE doesn't
 * allow multi-inheriting actor classes, so it implements
 * ISWGNetworkObjectInterface independently. See world-object-plan.html
 * "Actor hierarchy".
 */
UCLASS()
class SWGEMU_API ASWGObject : public AActor, public ISWGNetworkObjectInterface
{
	GENERATED_BODY()

public:
	ASWGObject() = default;

	int64  SWGObjectId  = 0;
	uint32 SWGObjectCRC = 0;
	bool   bBaselinesComplete = false;

	virtual int64 GetObjectId() const override { return SWGObjectId; }
	virtual void SetObjectId(int64 NewObjectId) override { SWGObjectId = NewObjectId; }

	virtual uint32 GetObjectCrc() const override { return SWGObjectCRC; }
	virtual void SetObjectCrc(uint32 NewObjectCrc) override { SWGObjectCRC = NewObjectCrc; }

	/** Called once SceneEndBaselines confirms this object is fully initialized. */
	virtual void OnBaselineComplete() {}
};
