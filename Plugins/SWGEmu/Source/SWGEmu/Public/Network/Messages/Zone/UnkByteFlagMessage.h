#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * UnkByteFlagMessage (opcode 0x7102B15F, opcount 0x02)
 *
 * Sent by the zone server as part of the post-CmdStartScene burst. Purpose
 * unconfirmed server-side (named "unkByteFlag" in Core3); always observed
 * carrying a single byte with value 1.
 *
 * Wire layout:
 *   [0x02][0x7102B15F] value(uint8)
 */
struct FUnkByteFlagMessage : public FSWGNetMessage
{
	uint8 Value = 0;

	FUnkByteFlagMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
