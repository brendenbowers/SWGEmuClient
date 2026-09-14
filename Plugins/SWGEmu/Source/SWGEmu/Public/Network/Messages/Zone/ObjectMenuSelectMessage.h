#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

/**
 * Tells the server which radial option the player picked (opcode 0x7CA18726,
 * opcount 2). Only sent for options whose Callback asks for it; the rest are
 * client-side commands.
 *
 * Wire layout (Core3 ObjectMenuSelectCallback::parse):
 *   [0x02][0x7CA18726] objectId(int64) radialId(u8)
 */
struct SWGEMU_API FObjectMenuSelectMessage
{
	uint64 ObjectId = 0;
	uint8 RadialId = 0;

	FSWGPacket Serialize() const;
};
