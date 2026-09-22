#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"

/**
 * Decoded state for an ITNO (Intangible Object) — datapad contents
 * (schematics, mission items, deeds...), which never get a spawned client
 * actor (see SWGFormTagMappings.csv's SITN row: "no visual actor - no world
 * presence"), so this is read straight off the wire by
 * USWGIntangibleObjectSubsystem instead of through the normal
 * actor-baseline dispatch.
 */
struct SWGEMU_API FIntangibleObjectBaseline
{
	float        UnknownVersion = 1.f;   // idx0, always 1.0 in retail
	FSWGStringId ObjectName;             // idx1
	FString      CustomName;             // idx2 (unicode)
	int32        Volume         = 0;     // idx3

	bool bHasBase3 = false;
};

namespace SWGIntangibleBaselineParser
{
	/** Parses an ITNO's base3 slot. */
	SWGEMU_API void ParseBase3(FSWGPacket& Packet, FIntangibleObjectBaseline& Out);
}
