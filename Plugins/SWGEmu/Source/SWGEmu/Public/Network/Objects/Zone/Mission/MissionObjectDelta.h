#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"

/**
 * Field-index deltas for a MISO's base3 slot — this is how a mission
 * terminal's offerings actually arrive; see MissionObjectBaseline.h for why
 * the baseline itself is blank. Indices verified against Core3's
 * MissionObjectDeltaMessage3 (server/zone/packets/mission/).
 */
struct SWGEMU_API FMissionObjectDelta
{
	TOptional<int32>        DifficultyDisplay;
	TOptional<FVector>      EndPosition; // startUpdate(0x06) "destination"
	TOptional<FString>      CreatorName;
	TOptional<int32>        RewardCredits;
	TOptional<FVector>      StartPosition;
	TOptional<uint32>       TargetTemplateCrc;
	TOptional<FSWGStringId> MissionDescription;
	TOptional<FSWGStringId> MissionTitle;
	TOptional<uint32>       RefreshCounter;
	TOptional<uint32>       TypeCRC;
	TOptional<FString>      TargetName;
};

namespace SWGMissionDeltaParser
{
	SWGEMU_API void ParseDelta3(FSWGPacket& Packet, FMissionObjectDelta& Out, uint16 UpdateCount);
}
