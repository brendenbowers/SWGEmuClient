#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * ErrorMessage (opcode 0xB5ABF91A, opcount 0x03)
 *
 * Sent by login or zone servers to report a rejected request (bad character ID,
 * server locked/full, zone disabled, etc). Not tied to any specific pending
 * opcode — it's a general "here's why your last request failed" reply, so
 * USWGMessageWaitSubsystem treats it as a broadcast failure for all waiters
 * rather than matching it to one.
 *
 * Wire layout:
 *   [0x03][0xB5ABF91A] errorType(ascii) errorMsg(ascii) fatal(uint8)
 */
struct SWGEMU_API FErrorMessage : public FSWGNetMessage
{
	FString ErrorType;
	FString ErrorMsg;
	uint8   Fatal = 0;

	FErrorMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
