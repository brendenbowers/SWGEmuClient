#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

/**
 * CmdSceneReadyMessage (opcode 0x43FD1C22, opcount 0x01)
 *
 * Sent by the client to the zone server after loading the scene from CmdStartScene.
 * No payload. The server responds by placing the player into the world.
 *
 * Wire layout:
 *   [0x01][0x43FD1C22]
 */
struct FCmdSceneReadyMessage
{
	FSWGPacket Serialize() const;
};
