#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/TangibleObjectBaseline.h"
#include "Network/Objects/Zone/Installation/HopperItem.h"

/**
 * Decoded state for an INSO (Installation) or HINO (Harvester). Both share
 * slots 6 and 7; their base3 layouts differ only in the tail past the tangible
 * fields, so each has its own parser.
 */
struct SWGEMU_API FInstallationObjectBaseline
{
	FTangibleObjectBaseline Tangible;

	// ── Base3 (INSO) ───────────────────────────────────────────────
	uint8 ActiveFlag    = 0;
	float SurplusPower  = 0.f;
	float BasePowerRate = 0.f;
	bool  bHasBase3     = false;

	bool bHasBase6 = false;

	// ── Base7 — the harvester UI's resource pool and hopper ────────
	TArray<uint64>      ResourceIds;
	TArray<FString>     ResourceNames;
	TArray<FString>     ResourceTypes;
	int64               ActiveResourceId = 0;
	uint8               bOperating = 0;
	int32               ExtractionRateDisplayed = 0;
	float               ExtractionRateMax = 0.f;
	float               CurrentExtractionRate = 0.f;
	float               HopperSize = 0.f;
	int32               HopperSizeMax = 0;
	uint8               HopperUpdateFlag = 0;
	TSWGBaselineList<FHopperItem> Hopper;
	uint8               ConditionPercent = 0;
	bool                bHasBase7 = false;
};

namespace SWGInstallationBaselineParser
{
	SWGEMU_API void ParseBase3(FSWGPacket& Packet, FInstallationObjectBaseline& Out);

	/** HINO's base3: the tangible fields, then two unlabelled values Core3 hardcodes. */
	SWGEMU_API void ParseHarvesterBase3(FSWGPacket& Packet, FInstallationObjectBaseline& Out);

	SWGEMU_API void ParseBase6(FSWGPacket& Packet, FInstallationObjectBaseline& Out);
	SWGEMU_API void ParseBase7(FSWGPacket& Packet, FInstallationObjectBaseline& Out);
}
