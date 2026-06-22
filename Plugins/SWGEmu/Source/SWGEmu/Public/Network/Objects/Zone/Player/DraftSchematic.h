#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FDraftSchematic
{
	uint32 ServerSchematicCRC = 0;
	uint32 SchematicCRC = 0;

	bool Deserialize(FSWGPacket& Packet)
	{
		Packet << ServerSchematicCRC;
		Packet << SchematicCRC;
		return true;
	}
};
