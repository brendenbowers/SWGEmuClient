#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

/**
 * ConnectPlayerMessage (opcode 0x2E365218, opcount 0x02)
 *
 * Sent by the client to the zone server after receiving ClientPermissionsMessage.
 * No payload — the server responds with ConnectPlayerResponseMessage and then
 * triggers the SelectCharacter / CmdStartScene sequence.
 *
 * Wire layout:
 *   [0x02][0x2E365218]
 */
struct SWGEMU_API FConnectPlayerMessage
{
	FSWGPacket Serialize() const;
};
