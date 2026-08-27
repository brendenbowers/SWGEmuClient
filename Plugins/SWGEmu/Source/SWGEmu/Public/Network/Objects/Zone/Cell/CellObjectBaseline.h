#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"

/**
 * Decoded state for an SCLT (Cell Object).
 *
 * Core3 writes the name/complexity/volume fields as hardcoded zeroes, so in
 * practice only CellNumber carries anything.
 */
struct SWGEMU_API FCellObjectBaseline
{
	float        Complexity = 0.f;
	FSWGStringId ObjectName;
	FString      CustomName;
	int32        Volume     = 0;
	int32        CellNumber = 0;
	bool         bHasBase3  = false;
};

namespace SWGCellBaselineParser
{
	SWGEMU_API void ParseBase3(FSWGPacket& Packet, FCellObjectBaseline& Out);
}
