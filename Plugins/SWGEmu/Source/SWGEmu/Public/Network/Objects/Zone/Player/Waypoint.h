#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FWaypoint
{
	int32 CellId = 0;
	float XCoord = 0.0f;
	float ZCoord = 0.0f;
	float YCoord = 0.0f;
	uint64 LocationNetworkId = 0;
	uint32 PlanetCRC = 0;
	FString WaypointName;
	uint64 WaypointObjectId = 0;
	uint8 Colour = 0;
	uint8 Active = 0;

	bool Deserialize(FSWGPacket& Packet)
	{
		Packet << CellId;
		Packet << XCoord;
		Packet << ZCoord;
		Packet << YCoord;
		Packet << LocationNetworkId;
		Packet << PlanetCRC;
		Packet << WaypointName;
		Packet << WaypointObjectId;
		Packet << Colour;
		Packet << Active;
		return true;
	}
};
