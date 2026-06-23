#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * ConnectPlayerResponseMessage (opcode 0x6137556F, opcount 0x02)
 *
 * Sent by the zone server in response to ConnectPlayerMessage.
 * Always contains a single int32 of value 0. After this, the server
 * sends SelectCharacter acknowledgement and CmdStartScene.
 *
 * Wire layout:
 *   [0x02][0x6137556F] unknown(int32=0)
 */
struct FConnectPlayerResponseMessage : public FSWGNetMessage
{
	int32 Unknown = 0;

	FConnectPlayerResponseMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
