#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * SceneCreateObjectMessage (opcode 0xFE89DDEA, opcount 0x05)
 *
 * Sent by the zone server once per visible object to spawn it client-side.
 * Followed by one or more BaselinesMessage packets (full state) and finally
 * a SceneEndBaselines packet once the object is fully initialized.
 *
 * Wire layout (matches Core3 SceneObjectCreateMessage):
 *   [0x05][0xFE89DDEA]
 *   objectId(int64)
 *   dirX(float) dirY(float) dirZ(float) dirW(float)   ← orientation quaternion
 *   posX(float) posZ(float) posY(float)                ← NOTE: Z before Y on wire
 *   objectCrc(uint32)                                  ← template CRC (client_object_crc)
 *   hyperspacing(uint8)
 */
struct SWGEMU_API FSceneCreateObjectMessage : public FSWGNetMessage
{
	int64  ObjectId       = 0;
	float  DirX           = 0.f;
	float  DirY           = 0.f;
	float  DirZ           = 0.f;
	float  DirW           = 0.f;
	float  PosX           = 0.f;
	float  PosZ           = 0.f;
	float  PosY           = 0.f;
	uint32 ObjectCrc      = 0;
	uint8  Hyperspacing   = 0;

	FSceneCreateObjectMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
