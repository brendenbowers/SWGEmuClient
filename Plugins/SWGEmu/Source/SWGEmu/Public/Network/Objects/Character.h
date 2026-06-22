#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FCharacter
{
	FString Name;
	uint32 RaceGenderCRC = 0;
	uint64 CharacterID = 0;
	uint32 ServerID = 0;
	uint32 Status = 0;

	bool Deserialize(FSWGPacket& Packet)
	{
		if (Packet.Tell() + 20 > Packet.TotalSize())
			return false;

		Packet << Name;
		Packet << RaceGenderCRC;
		Packet << CharacterID;
		Packet << ServerID;
		Packet << Status;
		return true;
	}
};
