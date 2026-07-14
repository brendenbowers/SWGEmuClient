#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Network/Objects/Zone/Player/Experience.h"
#include "Network/Objects/Zone/Player/Waypoint.h"
#include "Network/Objects/Zone/Player/QuestJournalItem.h"
#include "Network/Objects/Zone/Player/DraftSchematic.h"

/**
 * Decoded state for a PLAY (PlayerObject / "ghost") — the account/profile
 * layer attached to a player's CreatureObject. Base3 extends
 * IntangibleObjectMessage3 (a lighter version of Tangible3 — no
 * customization string / visible components / condition fields).
 *
 * Field order verified against Core3's PlayerObjectMessage3/6/8/9
 * (server/zone/packets/player/). Several Base9 fields are Core3 placeholders
 * (friends/ignore lists, unused crafting slots) that the live server always
 * sends as zero; they're still decoded so future server versions that
 * populate them don't desync the field layout.
 */
struct SWGEMU_API FPlayerObjectBaseline
{
	// ── Base3 ──────────────────────────────────────────────────────
	float        Complexity = 0.f; // Always 1 server-side
	FSWGStringId ObjectName;
	FString      CustomName;
	int32        Volume = 0;
	int32        Status = 0;
	TArray<uint32> PlayerBitmasks; // Always 4 entries
	FString      Title;
	int32        BirthDate = 0;
	int32        TotalPlayedTime = 0;
	bool bHasBase3 = false;

	// ── Base6 ──────────────────────────────────────────────────────
	uint8        PrivilegeFlag = 0; // Developer/CSR flag
	bool bHasBase6 = false;

	// ── Base8 ──────────────────────────────────────────────────────
	TSWGBaselineList<FExperience>       ExperienceList;
	TSWGBaselineList<FWaypoint>         WaypointList; // Key (LocationNetworkId map key) not retained separately — see FWaypoint
	int32  ForcePower    = 0;
	int32  ForcePowerMax = 0;
	TSWGBaselineList<uint8> CompletedQuests; // Bitfield, 8 quests per byte
	TSWGBaselineList<uint8> ActiveQuests;
	TSWGBaselineList<FQuestJournalItem> Quests;
	bool bHasBase8 = false;

	// ── Base9 ──────────────────────────────────────────────────────
	TSWGBaselineList<FString> AbilityList; // Certifications/command names
	TSWGBaselineList<FDraftSchematic> Schematics;
	int32 LanguageId = 0;
	int32 FoodFilling = 0;
	int32 FoodFillingMax = 0;
	int32 DrinkFilling = 0;
	int32 DrinkFillingMax = 0;
	int32 JediState = 0;
	bool bHasBase9 = false;
};

namespace SWGPlayerBaselineParser
{
	SWGEMU_API void ParseBase3(FSWGPacket& Packet, FPlayerObjectBaseline& Out);
	SWGEMU_API void ParseBase6(FSWGPacket& Packet, FPlayerObjectBaseline& Out);
	SWGEMU_API void ParseBase8(FSWGPacket& Packet, FPlayerObjectBaseline& Out);
	SWGEMU_API void ParseBase9(FSWGPacket& Packet, FPlayerObjectBaseline& Out);
}
