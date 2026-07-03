#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FExperience
{
	FString Type;
	int32 Value = 0;

	// Wire order (PlayerObject experienceList entry, a DeltaVectorMap<String, int>):
	//   name(ascii) value(int32). Leading map command byte consumed by the caller.
	bool Deserialize(FSWGPacket& Packet)
	{
		Type = Packet.ReadAsciiString();
		Value = Packet.ReadInt32();
		return true;
	}
};
