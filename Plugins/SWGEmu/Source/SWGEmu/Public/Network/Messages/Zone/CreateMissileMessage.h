#pragma once
#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * CreateMissileMessage (opcode 0x721CF08B, opcount 0x14). Core3 sends it when a ship launches a missile at a target:
 * [id i32][source i64][target i64][missile xyz f32][aim point xyz f32][seconds to hit i32][type i32][weapon slot i32][target slot i32].
 * The positions are Core3 raw floats in the wire's own axis order; this client only needs the ids and the time.
 */
struct SWGEMU_API FCreateMissileMessage : FSWGNetMessage
{
	int32 MissileId = 0;
	int64 SourceId = 0;
	int64 TargetId = 0;
	int32 SecondsToHit = 0;
	int32 MissileType = 0;
	int32 WeaponSlot = 0;
	int32 TargetSlot = 0;
	bool bValid = false;

	FCreateMissileMessage() = default;
	FCreateMissileMessage(uint32 Op, FSWGMessage& Reader) : FSWGNetMessage(Op, Reader) { bValid = Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};
