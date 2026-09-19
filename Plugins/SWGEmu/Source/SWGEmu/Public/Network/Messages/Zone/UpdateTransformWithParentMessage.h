#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * UpdateTransformWithParentMessage (opcode 0xC867AB5A, opcount 0x08) — the
 * in-cell counterpart of FUpdateTransformMessage, sent while an object moves
 * inside a building. Confirmed against Core3's
 * UpdateTransformWithParentMessage.h:
 *   [0x08][0xC867AB5A] parentId(long) objectId(long)
 *   posX(int16, *8 packed) posZ(int16, *8 packed) posY(int16, *8 packed)
 *   movementCounter(int32) currentSpeed(int8) directionAngle(uint8)
 *
 * Position is relative to the parent cell (cell-local is building-local),
 * packed as eighth-units — finer than the world message's quarters.
 */
struct SWGEMU_API FUpdateTransformWithParentMessage : public FSWGNetMessage
{
	int64 ParentId         = 0;
	int64 ObjectId         = 0;
	float PosX             = 0.f;
	float PosZ             = 0.f;
	float PosY             = 0.f;
	int32 MovementCounter  = 0;
	int8  CurrentSpeed     = 0;
	uint8 DirectionAngle   = 0;

	FUpdateTransformWithParentMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
