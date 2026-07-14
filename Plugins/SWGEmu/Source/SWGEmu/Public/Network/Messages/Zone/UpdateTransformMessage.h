#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * UpdateTransformMessage (opcode 0x1B24F808, opcount 0x08)
 *
 * Sent continuously while an object moves — this is what makes creatures and
 * players actually walk around instead of sitting frozen at their spawn
 * position. Confirmed against Core3's UpdateTransformMessage.h /
 * LightUpdateTransformMessage.h (identical wire layout, just two source-side
 * variants of the same message):
 *   [0x08][0x1B24F808] objectId(long)
 *   posX(int16, *4 packed) posZ(int16, *4 packed) posY(int16, *4 packed)
 *   movementCounter(int32) currentSpeed(int8) directionAngle(uint8)
 *
 * Position fields are the object's own PositionX/Y/Z (see
 * SceneCreateObjectMessage's identical X,Z,Y wire order/comment) packed as
 * quarter-units in an int16 for bandwidth — divide by 4 to get real world
 * units. DirectionAngle is a single byte encoding facing (0-255 mapping to
 * 0-360 degrees) — cheap per-tick facing without a full quaternion.
 */
struct SWGEMU_API FUpdateTransformMessage : public FSWGNetMessage
{
	int64 ObjectId         = 0;
	float PosX             = 0.f;
	float PosZ             = 0.f;
	float PosY             = 0.f;
	int32 MovementCounter  = 0;
	int8  CurrentSpeed     = 0;
	uint8 DirectionAngle   = 0;

	FUpdateTransformMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
