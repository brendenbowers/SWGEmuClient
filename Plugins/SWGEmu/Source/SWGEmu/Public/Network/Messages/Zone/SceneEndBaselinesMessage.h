#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * SceneEndBaselinesMessage (opcode 0x2C436037, opcount 0x02)
 *
 * Sent by the zone server once all BaselinesMessage slots for an object have
 * been transmitted — signals that the object is fully initialized and safe
 * to finalize client-side (e.g. attach to the world, show in UI).
 *
 * Wire layout (matches Core3 SceneObjectCloseMessage):
 *   [0x02][0x2C436037] objectId(int64)
 */
struct FSceneEndBaselinesMessage : public FSWGNetMessage
{
	int64 ObjectId = 0;

	FSceneEndBaselinesMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
