#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGMessage.h"

struct SWGEMU_API FServerName
{
	uint32 ServerID = 0;
	FString ServerDisplayName;
	uint32 Timezone = 0;

	bool Deserialize(FSWGMessage& Packet)
	{
		Packet >> ServerID;
		ServerDisplayName = Packet.ReadAsciiString();
		Packet >> Timezone;
		return true;
	}
};
