#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"

/**
 * Decoded state for a MISO (Mission Object) — the entries the player's
 * mission_bag holds, one per terminal-offered mission.
 *
 * Field order verified against Core3's MissionObjectMessage3
 * (server/zone/packets/mission/): every field past index 4 (Difficulty
 * onward) is written as zero in the baseline itself — a MISO's baseline is
 * created blank and MissionManager::populateMissionList fills it in
 * afterward entirely through MissionObjectDeltaMessage3 updates — so
 * FMissionObjectDelta below, not this struct, carries the values a mission
 * browser actually needs to display. Index positions cross-checked against
 * MissionObjectDeltaMessage3's startUpdate() calls (0x05..0x10, consecutive,
 * matching declaration order 1:1 — 0x00..0x04 are baseline-only/legacy).
 */
struct SWGEMU_API FMissionObjectBaseline
{
	// ── Base3 ──────────────────────────────────────────────────────
	float        UnknownVersion   = 1.f; // idx0, always 1.0 in retail
	FSWGStringId ObjectName;              // idx1
	FString      CustomName;              // idx2 (unicode)
	int32        Volume           = 0;    // idx3, always 0
	int32        Unused4          = 0;    // idx4, legacy desc/title key slot, unused

	int32        DifficultyDisplay = 0;   // idx5

	// idx6 — "destination" (the mission's end position)
	FVector      EndPosition      = FVector::ZeroVector; // x, z, y
	int64        EndObjectId      = 0;
	uint32       EndPlanetCrc     = 0;

	FString      CreatorName      = FString(); // idx7 (unicode)
	int32        RewardCredits    = 0;         // idx8

	// idx9 — mission start position
	FVector      StartPosition    = FVector::ZeroVector; // x, z, y
	int64        StartObjectId    = 0;
	uint32       StartPlanetCrc   = 0;

	uint32       TargetTemplateCrc = 0; // idxA

	FSWGStringId MissionDescription; // idxB
	FSWGStringId MissionTitle;       // idxC

	int32        RefreshCounter   = 0; // idxD, always 0 in baseline
	uint32       TypeCRC          = 0; // idxE
	FString      TargetName       = FString(); // idxF (ascii)

	// idx10 — waypoint info; almost always the "no waypoint" shape at baseline time.
	int32        WaypointUnknown  = 0;
	FVector      WaypointPosition = FVector::ZeroVector;
	int64        WaypointTargetId = 0;
	uint32       WaypointPlanetCrc = 0;
	FString      WaypointName     = FString();
	int64        WaypointObjectId = 0;
	uint8        WaypointColor    = 0;
	uint8        WaypointActive   = 0;

	bool bHasBase3 = false;
};

namespace SWGMissionBaselineParser
{
	/** Parses a MISO's base3 slot. */
	SWGEMU_API void ParseBase3(FSWGPacket& Packet, FMissionObjectBaseline& Out);
}
