#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FServerDetails
{
	uint32 ServerID = 0;
	FString ServerIP;
	uint16 ServerPort = 0;
	uint16 PingPort = 0;
	uint32 Population = 0;
	uint32 MaxCapacity = 0;
	uint32 MaxCharsPerServer = 0;
	uint32 Distance = 0;
	uint32 Status = 0;
	bool bNotRecommended = false;

	bool Deserialize(FSWGPacket& Packet)
	{
		Packet << ServerID;
		Packet << ServerIP;
		Packet << ServerPort;
		Packet << PingPort;
		Packet << Population;
		Packet << MaxCapacity;
		Packet << MaxCharsPerServer;
		Packet << Distance;
		Packet << Status;
		Packet << bNotRecommended;
		return true;
	}
};
