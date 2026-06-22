#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FGroupMissionCriticalObject
{
	uint64 MissionOwnerId = 0;
	uint64 MissionCriticalObjectId = 0;

	bool Deserialize(FSWGPacket& Packet)
	{
		Packet << MissionOwnerId;
		Packet << MissionCriticalObjectId;
		return true;
	}
};
