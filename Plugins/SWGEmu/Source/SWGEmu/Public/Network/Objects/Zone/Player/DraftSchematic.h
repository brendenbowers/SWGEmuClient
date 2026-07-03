#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FDraftSchematic
{
	uint32 ServerSchematicCRC = 0;
	uint32 SchematicCRC = 0;

	// Wire order (SchematicList::insertToMessage — the client CRC is written twice):
	//   clientObjectCrc(uint32) clientObjectCrc(uint32)
	bool Deserialize(FSWGPacket& Packet)
	{
		ServerSchematicCRC = Packet.ReadUInt32();
		SchematicCRC = Packet.ReadUInt32();
		return true;
	}
};
