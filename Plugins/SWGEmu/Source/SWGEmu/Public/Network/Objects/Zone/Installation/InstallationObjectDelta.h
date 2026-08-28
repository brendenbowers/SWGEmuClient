#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/TangibleObjectDelta.h"
#include "Network/Objects/Zone/Installation/HopperItem.h"

/** Decoded updates from an INSO/HINO delta slot. Slots 3 and 6 extend the tangible ones. */
struct SWGEMU_API FInstallationObjectDelta
{
	FTangibleObjectDelta Tangible;

	// ── Base3 ──────────────────────────────────────────────────────
	TOptional<uint8> ActiveFlag;
	TOptional<float> SurplusPower;
	TOptional<float> BasePowerRate;

	// ── Base7 ──────────────────────────────────────────────────────
	TSWGListChanges<uint64>     ResourceIds;
	TSWGListChanges<FString>    ResourceNames;
	TSWGListChanges<FString>    ResourceTypes;
	TOptional<int64>            ActiveResourceId;
	TOptional<uint8>            bOperating;
	TOptional<int32>            ExtractionRateDisplayed;
	TOptional<float>            ExtractionRateMax;
	TOptional<float>            CurrentExtractionRate;
	TOptional<float>            HopperSize;
	TOptional<int32>            HopperSizeMax;
	TOptional<uint8>            HopperUpdateFlag;
	TSWGListChanges<FHopperItem> Hopper;
	TOptional<uint8>            ConditionPercent;
};

namespace SWGInstallationDeltaParser
{
	SWGEMU_API void ParseDelta3(FSWGPacket& Packet, FInstallationObjectDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta6(FSWGPacket& Packet, FInstallationObjectDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta7(FSWGPacket& Packet, FInstallationObjectDelta& Out, uint16 UpdateCount);
}
