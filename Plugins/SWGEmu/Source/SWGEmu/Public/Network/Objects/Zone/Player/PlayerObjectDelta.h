#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Network/Objects/Zone/Object/SWGDeltaListHelpers.h"
#include "Network/Objects/Zone/Player/Experience.h"
#include "Network/Objects/Zone/Player/Waypoint.h"
#include "Network/Objects/Zone/Player/QuestJournalItem.h"
#include "Network/Objects/Zone/Player/DraftSchematic.h"

/**
 * Decoded updates from a PLAY delta slot. PLAY slots don't extend the tangible
 * ones — every slot is its own layout.
 */
struct SWGEMU_API FPlayerObjectDelta
{
	// ── Base3 ──────────────────────────────────────────────────────
	TOptional<TArray<uint32>> PlayerBitmasks;
	TOptional<FString>        Title;
	TOptional<int32>          BirthDate;
	TOptional<int32>          TotalPlayedTime;

	// ── Base6 ──────────────────────────────────────────────────────
	TOptional<uint8> PrivilegeFlag;

	// ── Base8 ──────────────────────────────────────────────────────
	TSWGListChanges<FExperience>       ExperienceList;
	TSWGListChanges<FWaypoint>         WaypointList;
	TOptional<int32>                   ForcePower;
	TOptional<int32>                   ForcePowerMax;
	TSWGListChanges<uint8>             CompletedQuests;
	TSWGListChanges<uint8>             ActiveQuests;
	TSWGListChanges<FQuestJournalItem> Quests;

	// ── Base9 ──────────────────────────────────────────────────────
	TSWGListChanges<FString>          AbilityList;
	TOptional<int32>                  ExperimentationFlag;
	TOptional<int32>                  CraftingState;
	TOptional<int64>                  ClosestCraftingStation;
	TSWGListChanges<FDraftSchematic>  Schematics;
	TOptional<int32>                  ExperimentationPoints;
	TSWGListChanges<FString>          FriendsList;
	TSWGListChanges<FString>          IgnoreList;
	TOptional<int32>                  LanguageId;
	TOptional<int32>                  FoodFilling;
	TOptional<int32>                  FoodFillingMax;
	TOptional<int32>                  DrinkFilling;
	TOptional<int32>                  DrinkFillingMax;
	TOptional<int32>                  JediState;
};

namespace SWGPlayerDeltaParser
{
	SWGEMU_API void ParseDelta3(FSWGPacket& Packet, FPlayerObjectDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta6(FSWGPacket& Packet, FPlayerObjectDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta8(FSWGPacket& Packet, FPlayerObjectDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta9(FSWGPacket& Packet, FPlayerObjectDelta& Out, uint16 UpdateCount);
}
