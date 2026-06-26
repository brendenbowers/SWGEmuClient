#pragma once

#include "CoreMinimal.h"

/** SessionData is extracted from the SOE SessionResponse packet and passed to handlers for initialization. */
struct SessionData
{
	uint32 EncryptionKey = 0;
	uint8 UseCompression = 0;
	uint32 MaxPacketSize = 0;
};
