#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/TangibleObjectDelta.h"

/** Decoded updates from an RCNO delta slot. Slot 3 extends the tangible one. */
struct SWGEMU_API FResourceContainerObjectDelta
{
	FTangibleObjectDelta Tangible;

	// ── Base3 ──────────────────────────────────────────────────────
	TOptional<int32> Quantity;
	TOptional<int64> SpawnId;

	// ── Base6 ──────────────────────────────────────────────────────
	TOptional<FString> ResourceType;
	TOptional<FString> ResourceName;
};

namespace SWGResourceContainerDeltaParser
{
	SWGEMU_API void ParseDelta3(FSWGPacket& Packet, FResourceContainerObjectDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta6(FSWGPacket& Packet, FResourceContainerObjectDelta& Out, uint16 UpdateCount);
}
