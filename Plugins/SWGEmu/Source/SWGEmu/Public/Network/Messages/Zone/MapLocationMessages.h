#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"
#include "Network/SWGPacket.h"

/** One Core3 planetary-map entry. Coordinates are raw east/north metres. */
struct SWGEMU_API FSWGMapLocation
{
	uint64 ObjectId = 0;
	FString Name;
	FVector2D Position = FVector2D::ZeroVector;
	uint8 Category = 0;
	uint8 Subcategory = 0;
	uint8 Icon = 0;
};

struct SWGEMU_API FGetMapLocationsResponseMessage : public FSWGNetMessage
{
	FString Planet;
	TArray<FSWGMapLocation> Locations;
	bool bValid = false;

	FGetMapLocationsResponseMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { bValid = Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};

struct FGetMapLocationsRequestMessage
{
	FString Planet;

	FSWGPacket Serialize() const
	{
		FSWGPacket Packet;
		Packet.WriteUInt16(0x02);
		Packet.WriteUInt32(0x1A7AB839u);
		Packet.WriteAsciiString(Planet);
		return Packet;
	}
};
