#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FGroupMissionCriticalObject
{
	uint64 MissionOwnerId = 0;
	uint64 MissionCriticalObjectId = 0;

	// Wire order (CreatureObjectMessage4 spaceMissionObjects, a DeltaSet<uint64,uint64> pair):
	//   ownerId(int64) objectId(int64) — no per-entry command byte in a baseline dump.
	bool Deserialize(FSWGPacket& Packet)
	{
		MissionOwnerId = Packet.ReadUInt64();
		MissionCriticalObjectId = Packet.ReadUInt64();
		return true;
	}
};
