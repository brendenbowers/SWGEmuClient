#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * ParametersMessage (opcode 0x487652DA, opcount 0x02)
 *
 * Sent by the zone server as part of the post-CmdStartScene burst. Carries a
 * single fixed parameter (observed value 0x00000384 = 900, likely a timeout
 * or tick interval in milliseconds).
 *
 * Wire layout:
 *   [0x02][0x487652DA] value(uint32)
 */
struct SWGEMU_API FParametersMessage : public FSWGNetMessage
{
	uint32 Value = 0;

	FParametersMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
