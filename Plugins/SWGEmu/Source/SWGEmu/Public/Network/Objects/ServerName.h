#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FServerName
{
	uint32 ServerID = 0;
	FString ServerDisplayName;
	uint32 Timezone = 0;

	bool Deserialize(FSWGPacket& Packet)
	{
		Packet << ServerID;
		Packet << ServerDisplayName;
		Packet << Timezone;
		return true;
	}
};
