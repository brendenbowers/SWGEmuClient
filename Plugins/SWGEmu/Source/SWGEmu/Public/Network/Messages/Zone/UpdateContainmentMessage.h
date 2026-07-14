#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * UpdateContainmentMessage (opcode 0x56CBDE9E, opcount 0x04)
 *
 * Sent whenever an object's container changes — attaching an item to a
 * player (equip/inventory pickup) or detaching it (drop/unequip). Confirmed
 * against Core3's UpdateContainmentMessage.h: [0x04][0x56CBDE9E] objectId
 * (long) containerId (long) type (int). ContainerId is 0 when the object has
 * no container (placed directly in the world).
 *
 * "Type" is Core3's "containmentType"/"arrangement type" (SceneObject.idl) —
 * which slot/arrangement within the container, e.g. PlayerArrangement enum
 * values for equip slots vs vehicle seats. Not needed for basic show/hide;
 * kept for whenever real per-slot placement is added.
 */
struct SWGEMU_API FUpdateContainmentMessage : public FSWGNetMessage
{
	int64  ObjectId    = 0;
	int64  ContainerId = 0;
	uint32 Type        = 0;

	FUpdateContainmentMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
