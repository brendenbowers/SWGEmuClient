#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"
#include "Network/SWGPacket.h"

/**
 * 
 */
struct SWGEMU_API FObjectControllerMessage
{
public:
	uint32 MessageType;
	uint32 MessagePriority = 1;
	uint64 ObjectId;

	~FObjectControllerMessage() = default;
protected:

	FObjectControllerMessage(uint32 MessageType, uint64 ObjectId, uint32 MessagePriority = 1);
	
	FSWGPacket SerializeBase(uint16 OpcodeCount) const;
};
