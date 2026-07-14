#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct SWGEMU_API FStringFile
{
	FString STFFile;
	FString STFName;

	bool Deserialize(FSWGPacket& Packet)
	{
		Packet << STFFile;
		Packet.Seek(Packet.Tell() + 1);
		Packet << STFName;
		return true;
	}
};
