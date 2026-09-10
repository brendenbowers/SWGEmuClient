#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * A system message (opcode 0x6D2A6413) — the channel the server explains
 * itself on, and the only feedback for a whole class of command failures.
 * GENERALERROR, INVALIDWEAPON and friends reply with a zero error code that
 * is indistinguishable from success, and say why only here.
 *
 * Wire layout (Core3 ChatSystemMessage):
 *   displayType(byte) message(unicode) paramsSize(int32)
 *
 * Two shapes share it. sendSystemMessage(String) puts the whole text —
 * usually a "@file:key" stringfile reference — in Message and leaves
 * paramsSize 0. The StringIdChatParameter form instead leaves Message empty
 * and follows paramsSize with a parameter blob, which is not decoded here;
 * such a message arrives with both fields empty rather than misparsed.
 */
struct SWGEMU_API FChatSystemMessage : public FSWGNetMessage
{
	/** 0 shows in chat and on screen, 2 chat only. */
	uint8 DisplayType = 0;

	/** The text, or a "@file:key" reference to it. Empty for the parameterised shape. */
	FString Message;

	FChatSystemMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
