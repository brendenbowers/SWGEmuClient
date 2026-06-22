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

	virtual ~FSWGNetMessage() = default;

	/** Populate fields from the message reader. Cursor is at payload start (post-opcode). */
	virtual bool Deserialize(FSWGMessage& Reader) { return true; }
};
