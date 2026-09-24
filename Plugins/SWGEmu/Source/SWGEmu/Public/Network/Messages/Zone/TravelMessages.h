#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"
#include "Network/SWGPacket.h"

/** The terminal's departure point. Opening this message opens ticket purchase mode. */
struct SWGEMU_API FEnterTicketPurchaseModeMessage : public FSWGNetMessage
{
	FString DeparturePlanet;
	FString DepartureLocation;

	FEnterTicketPurchaseModeMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};

/** A server-defined destination accepted verbatim by purchaseTicket. */
struct SWGEMU_API FSWGPlanetTravelPoint
{
	FString Name;
	float X = 0.f;
	float Y = 0.f;
	float Z = 0.f;
	uint32 Tax = 0;
	bool bInterplanetary = false;
};

/** The exact travel-point names and properties configured by Core3 for a planet. */
struct SWGEMU_API FPlanetTravelPointListResponseMessage : public FSWGNetMessage
{
	FString Planet;
	TArray<FSWGPlanetTravelPoint> Points;

	FPlanetTravelPointListResponseMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};

/** Requests the server's ticket destinations for one planet. */
struct FPlanetTravelPointListRequestMessage
{
	FString Planet;

	FSWGPacket Serialize() const
	{
		FSWGPacket Packet;
		Packet.WriteUInt16(0x03);
		Packet.WriteUInt32(0x96405D4Du);
		Packet.WriteUInt64(0); // Core3 retains but ignores the retail player's object-id field.
		Packet.WriteAsciiString(Planet);
		return Packet;
	}
};
