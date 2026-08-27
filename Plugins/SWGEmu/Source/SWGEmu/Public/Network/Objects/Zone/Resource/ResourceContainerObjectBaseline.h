#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/TangibleObjectBaseline.h"

/**
 * Decoded state for an RCNO (Resource Container).
 *
 * Base3 extends TangibleObjectMessage3; base6 does not extend
 * TangibleObjectMessage6 and is a layout of its own.
 */
struct SWGEMU_API FResourceContainerObjectBaseline
{
	FTangibleObjectBaseline Tangible;

	// ── Base3 ──────────────────────────────────────────────────────
	int32 Quantity  = 0;
	int64 SpawnId   = 0;
	bool  bHasBase3 = false;

	// ── Base6 ──────────────────────────────────────────────────────
	FString ContainerName;
	int32   MaxStackSize = 0;
	FString ResourceType;   // Planet-specific spawn type
	FString ResourceName;
	bool    bHasBase6 = false;
};

namespace SWGResourceContainerBaselineParser
{
	SWGEMU_API void ParseBase3(FSWGPacket& Packet, FResourceContainerObjectBaseline& Out);
	SWGEMU_API void ParseBase6(FSWGPacket& Packet, FResourceContainerObjectBaseline& Out);
}
