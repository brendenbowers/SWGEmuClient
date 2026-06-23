#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

struct FSWGMessage;

/**
 * LoginClientTokenMessage (opcode 0xAAB296C6)
 *
 * Sent by the login server after successful authentication.
 *
 * Wire layout (at payload cursor):
 *   blobSize(4)  tokenBytes(blobSize-4)  accountID(4)  stationID(4)  userName(ascii)
 *
 * The server embeds accountID as the last 4 bytes of the blob,
 * so blobSize = tokenLength + 4. The full blob is stored in SessionKey.
 */
struct FLoginClientTokenMessage : public FSWGNetMessage
{
	int32         SessionKeySize = 0;
	TArray<uint8> SessionKey;
	uint32        StationID = 0;
	FString       UserName;

	FLoginClientTokenMessage(uint32 OPCode, FSWGMessage& Reader)
		: FSWGNetMessage(OPCode, Reader)
	{
		Deserialize(Reader);
	}

	bool Deserialize(FSWGMessage& Reader);
};
