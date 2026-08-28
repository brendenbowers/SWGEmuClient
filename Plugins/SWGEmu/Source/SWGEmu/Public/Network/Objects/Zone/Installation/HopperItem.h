#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct SWGEMU_API FHopperItem
{
	uint64 SpawnObjectId = 0;
	float  Quantity      = 0.f;

	// Wire order (HopperList entry): spawnObjectId(int64) quantity(float)
	bool Deserialize(FSWGPacket& Packet)
	{
		SpawnObjectId = Packet.ReadUInt64();
		Quantity = Packet.ReadFloat();
		return true;
	}
};
