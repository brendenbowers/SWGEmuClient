#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * ClientPermissionsMessage (opcode 0xE00730E5, opcount 0x04)
 *
 * Sent by the zone server after processing ClientIdMessage.
 * Indicates whether the client may enter the galaxy and create characters.
 *
 * Wire layout:
 *   [0x04][0xE00730E5] galaxyOpen(bool) canCreateChar(bool) unlimitedCreation(bool) unknown(bool)
 */
struct SWGEMU_API FClientPermissionsMessage : public FSWGNetMessage
{
	uint8 GalaxyOpen        = 0;
	uint8 CanCreateChar     = 0;
	uint8 UnlimitedCreation = 0;
	uint8 UnknownFlag       = 0;

	FClientPermissionsMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
