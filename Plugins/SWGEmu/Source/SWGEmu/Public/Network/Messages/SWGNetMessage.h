#pragma once

#include "CoreMinimal.h"

struct FSWGMessage;

/**
 * FSWGNetMessage is the base for all typed game messages.
 *
 * Concrete messages inherit from this, declare their fields, and implement
 * Deserialize() to read from the reader cursor. The opcode is populated by
 * the registry before Deserialize() is called.
 */
struct SWGEMU_API FSWGNetMessage
{
	uint32 Opcode = 0;
	FSWGNetMessage() = default;
	/** Construct from opcode and reader. Derived classes should call Deserialize() in their ctor. */
	FSWGNetMessage(uint32 OPCode, FSWGMessage& /*Reader*/)
		: Opcode(OPCode)
	{
		/* Reader is not used in base class, its there to satisfy a signature requirement with the registry */
	}
	virtual ~FSWGNetMessage() = default;

	/** Populate fields from the message reader. Cursor is at payload start (post-opcode). */
	//virtual bool Deserialize(FSWGMessage& Reader) { return true; }
};
