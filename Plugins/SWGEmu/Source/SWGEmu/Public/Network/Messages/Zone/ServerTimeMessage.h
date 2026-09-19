#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * ServerTimeMessage (opcode 0x2EBC3BD9, opcount 0x02)
 *
 * The zone's galactic time in seconds (Core3: seconds since the zone
 * started), sent once a minute and also carried by CmdStartScene. Drives
 * the day/night cycle: the planet's colour ramp is sampled at
 * (time mod FSWGTerrainHeader::TimeCycle) / TimeCycle.
 *
 * Wire layout:
 *   [0x02][0x2EBC3BD9] galacticTime(int64)
 */
struct SWGEMU_API FServerTimeMessage : public FSWGNetMessage
{
	int64 GalacticTime = 0;

	FServerTimeMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
