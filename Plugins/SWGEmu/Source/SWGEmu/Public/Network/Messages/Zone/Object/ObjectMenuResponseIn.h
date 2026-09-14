#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/Zone/Object/ObjectMenuRequest.h"

struct FSWGPacket;

/**
 * The server's radial menu for an object (sub-opcode 0x147): our
 * ObjectMenuRequest items merged with whatever the object's menu component
 * added, flattened parent-first.
 *
 * Payload (Core3 ObjectMenuResponse):
 *   targetId(int64) playerId(int64)
 *   count(int32) { index(u8) parent(u8) radialId(u8) callback(u8) text(unicode) }*
 *   counter(u8)
 */
struct SWGEMU_API FObjectMenuResponseIn
{
	uint64 TargetId = 0;
	uint64 PlayerId = 0;
	TArray<FSWGRadialMenuEntry> Items;
	uint8 Counter = 0;

	bool Parse(FSWGPacket& Packet);
};
