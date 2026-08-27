#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * SceneDestroyObjectMessage (opcode 0x4D45D504, opcount 0x03)
 *
 * Sent when an object leaves the client's view: a player logging out, an NPC
 * despawning, an item destroyed or moved out of range.
 *
 * Wire layout:
 *   [0x03][0x4D45D504] objectId(int64) hyperspace(uint8)
 */
struct SWGEMU_API FSceneDestroyObjectMessage : public FSWGNetMessage
{
	int64 ObjectId    = 0;
	uint8 bHyperspace = 0;

	FSceneDestroyObjectMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
