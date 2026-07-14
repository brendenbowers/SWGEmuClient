#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SWGNetworkObjectInterface.generated.h"

UINTERFACE(MinimalAPI)
class USWGNetworkObjectInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by every actor spawned from a SceneCreateObjectByCrc — both the
 * plain-AActor branch (ASWGObject and its children) and the ACharacter branch
 * (ASWGCreature), which can't share a common actor base. Lets
 * USWGObjectGraphSubsystem spawn/register/look up either kind by ObjectId
 * without caring which branch it came from.
 */
class SWGEMUCLIENT_API ISWGNetworkObjectInterface
{
	GENERATED_BODY()

public:
	virtual int64 GetObjectId() const = 0;
	virtual void SetObjectId(int64 NewObjectId) = 0;

	virtual uint32 GetObjectCrc() const = 0;
	virtual void SetObjectCrc(uint32 NewObjectCrc) = 0;
};
