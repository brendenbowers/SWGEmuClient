#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"
#include "Network/Objects/Character.h"

struct FSWGMessage;

/**
 * EnumerateCharacterIdMessage (opcode 0x65EA4574)
 *
 * Sent by the login server listing characters for the account.
 *
 * Wire layout (at payload cursor):
 *   count(4)  [ name(unicode) raceCRC(4) objectID(8) galaxyID(4) status(4) ] x count
 */
struct SWGEMU_API FEnumerateCharacterIdMessage : public FSWGNetMessage
{
	TArray<FCharacter> Characters;

	FEnumerateCharacterIdMessage(uint32 OPCode, FSWGMessage& Reader)
		: FSWGNetMessage(OPCode, Reader)
	{
		Deserialize(Reader);
	}

	bool Deserialize(FSWGMessage& Reader);
};
