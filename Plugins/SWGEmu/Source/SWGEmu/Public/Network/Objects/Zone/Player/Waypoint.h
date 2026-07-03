#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FWaypoint
{
	uint64 MapKey = 0; // WaypointList's DeltaVectorMap key — read by the caller, not this struct
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

	// Wire order (WaypointObjectImplementation::insertToMessage). The DeltaVectorMap
	// key (waypoint object id used as the map key) is read separately by the caller
	// before this — LocationNetworkId here is a distinct always-present "unknown" field.
	bool Deserialize(FSWGPacket& Packet)
	{
		CellId = Packet.ReadInt32();
		XCoord = Packet.ReadFloat();
		ZCoord = Packet.ReadFloat();
		YCoord = Packet.ReadFloat();
		LocationNetworkId = Packet.ReadUInt64();
		PlanetCRC = Packet.ReadUInt32();
		WaypointName = Packet.ReadUnicodeString();
		WaypointObjectId = Packet.ReadUInt64();
		Colour = Packet.ReadByte();
		Active = Packet.ReadByte();
		return true;
	}
};
