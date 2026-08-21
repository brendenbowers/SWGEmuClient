#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/Zone/ObjectControllerMessage.h"

struct SWGEMU_API FDataTransformWithParent : public FObjectControllerMessage
{
public:
	FQuat Direction;
	FVector Position;
	float Speed;
	uint32 TimeStamp;
	uint32 MoveCount;
	uint64 ParentId;

	FDataTransformWithParent(uint64 ObjectId);
	~FDataTransformWithParent() = default;

	FSWGPacket Serialize() const;
};
