#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FEquiptmentItem
{
	FString CustomizationString;
	int32 ContainmentType = 0;
	uint64 ObjectId = 0;
	uint32 TemplateCRC = 0;

	bool Deserialize(FSWGPacket& Packet)
	{
		Packet << CustomizationString;
		Packet << ContainmentType;
		Packet << ObjectId;
		Packet << TemplateCRC;
		return true;
	}
};
