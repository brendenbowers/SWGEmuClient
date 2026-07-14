#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/Zone/ObjectControllerMessage.h"

/**
 * 
 */
struct SWGEMU_API FDataTransform : public FObjectControllerMessage
{
public:
	FQuat Direction;
	FVector Position;
	float Speed;
	uint32 TimeStamp;
	uint32 MoveCount;

	FDataTransform(uint64 ObjectId);
	~FDataTransform() = default;

	FSWGPacket Serialize() const;
};
