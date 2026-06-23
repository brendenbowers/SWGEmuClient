#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

/**
 * SelectCharacterMessage (opcode 0xB5098D76, opcount 0x02)
 *
 * Sent by the client to the zone server to select a character for login.
 * Must include the SWGEmu server hash to authenticate against SWGEmu builds.
 *
 * Wire layout:
 *   [0x02][0xB5098D76] charID(int64) swgemuHash(uint32=0x928D8F6A)
 */
struct FSelectCharacterMessage
{
	int64 CharacterID = 0;

	// STRING_HASHCODE("SWGEmu") — required by SWGEmu zone servers
	static constexpr uint32 SWGEmuHash = 0x928D8F6Au;

	FSWGPacket Serialize() const;
};
