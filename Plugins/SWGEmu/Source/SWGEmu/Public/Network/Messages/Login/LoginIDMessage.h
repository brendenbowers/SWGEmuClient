#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

/**
 * LoginIDMessage (opcode 0x41131F96, opcount 0x04)
 *
 * First message sent by the client after the SOE handshake completes.
 * Authenticates the account with the login server.
 *
 * Wire layout:
 *   [0x04][0x41131F96] username(ascii) password(ascii) version(ascii)
 */
struct SWGEMU_API FLoginIDMessage
{
	FString Username;
	FString Password;

	static const FString ClientVersion;

	FSWGPacket Serialize() const;
};
