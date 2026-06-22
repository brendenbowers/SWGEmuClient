#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FExperience
{
	FString Type;
	int32 Value = 0;

	bool Deserialize(FSWGPacket& Packet)
	{
		Packet << Type;
		Packet << Value;
		return true;
	}
};
