#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FEquiptmentItem
{
	FString CustomizationString;
	int32 ContainmentType = 0;
	uint64 ObjectId = 0;
	uint32 TemplateCRC = 0;

	// Wire order (WearablesDeltaVector::insertItemToMessage):
	//   customizationString(ascii) containmentType(int32) objectId(int64) templateCrc(uint32)
	bool Deserialize(FSWGPacket& Packet)
	{
		CustomizationString = Packet.ReadAsciiString();
		ContainmentType = Packet.ReadInt32();
		ObjectId = Packet.ReadUInt64();
		TemplateCRC = Packet.ReadUInt32();
		return true;
	}
};
