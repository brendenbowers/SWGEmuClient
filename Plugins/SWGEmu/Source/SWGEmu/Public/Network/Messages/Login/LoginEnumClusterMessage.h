#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"
#include "Network/Objects/ServerName.h"

struct FSWGMessage;

/**
 * LoginEnumClusterMessage (opcode 0xC11C63B9)
 *
 * Sent by the login server listing available galaxy names.
 *
 * Wire layout (at payload cursor):
 *   count(4)  [ serverID(4) displayName(ascii) timezone(4) ] x count  maxCharsPerAccount(4)
 */
struct FLoginEnumClusterMessage : public FSWGNetMessage
{
	TArray<FServerName> Servers;
	int32               MaxCharactersPerAccount = 0;

	FLoginEnumClusterMessage(uint32 OPCode, FSWGMessage& Reader)
		: FSWGNetMessage(OPCode, Reader)
	{
		Deserialize(Reader);
	}

	bool Deserialize(FSWGMessage& Reader);
};
