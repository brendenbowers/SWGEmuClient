#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

/**
 * ClientIdMessage (opcode 0xD5899226, opcount 0x03)
 *
 * First message sent by the client to the zone server after the SOE handshake.
 * Passes the full session key blob received from the login server in
 * FLoginClientTokenMessage::SessionKey — the last 4 bytes are the embedded accountID.
 *
 * Wire layout:
 *   [0x03][0xD5899226] gameBits(0xFE) blobSize(int32) sessionKeyBytes[blobSize] version(ascii)
 *
 * Server reads: gameBits, totalSize, (totalSize-4) token bytes, 4-byte accountID, version string.
 */
struct SWGEMU_API FClientIdMessage
{
	TArray<uint8> SessionKey;
	FString       ClientVersion;

	static const FString DefaultClientVersion;

	FSWGPacket Serialize() const;
};
