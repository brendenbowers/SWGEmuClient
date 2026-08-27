#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGDeltaListHelpers.h"

/** Decoded updates from an SCLT delta slot. */
struct SWGEMU_API FCellObjectDelta
{
	TOptional<int32> CellNumber;
};

namespace SWGCellDeltaParser
{
	SWGEMU_API void ParseDelta3(FSWGPacket& Packet, FCellObjectDelta& Out, uint16 UpdateCount);
}
