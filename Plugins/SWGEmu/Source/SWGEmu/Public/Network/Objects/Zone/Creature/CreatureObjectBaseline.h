#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Network/Objects/Zone/Object/TangibleObjectBaseline.h"
#include "Network/Objects/Zone/Creature/EquiptmentItem.h"
#include "Network/Objects/Zone/Creature/SkillModifier.h"
#include "Network/Objects/Zone/Creature/GroupMissionCriticalObject.h"

/**
 * Decoded state for a CREO (Creature Object) — covers both NPCs and player
 * characters. CreatureObjectMessage3/6 extend TangibleObjectMessage3/6, so
 * Tangible holds those shared fields; the members below are CREO-only.
 *
 * Field order verified against Core3's CreatureObjectMessage1/3/4/6
 * (server/zone/packets/creature/). Slot 1 and 4 are standalone BaseLineMessages
 * (not Tangible-derived); slots 3 and 6 append to the Tangible3/6 payload.
 */
struct SWGEMU_API FCreatureObjectBaseline
{
	FTangibleObjectBaseline Tangible;

	// ── Base1 ──────────────────────────────────────────────────────
	int32  BankCredits = 0;
	int32  CashCredits = 0;
	TSWGBaselineList<int32>  BaseHAM;    // 9 pools: health/str/con/action/qck/sta/mind/foc/wil
	TSWGBaselineList<FString> SkillList; // Skill command/box names known
	bool bHasBase1 = false;

	// ── Base3 (appended after Tangible3) ─────────────────────────────
	uint8  Posture          = 0;
	uint8  FactionRank      = 0;
	int64  CreatureLinkId   = 0; // Mount object id, 0 if none
	float  Height           = 0.f;
	int32  ShockWounds      = 0;
	int64  StateBitmask     = 0;
	TSWGBaselineList<int32> Wounds;
	bool bHasBase3 = false;

	// ── Base4 ──────────────────────────────────────────────────────
	float  AccelerationMultiplierBase = 0.f;
	float  AccelerationMultiplierMod  = 0.f;
	TSWGBaselineList<int32> Encumbrances;
	TSWGBaselineList<FSkillModifier> SkillMods;
	float  SpeedMultiplierBase = 0.f;
	float  SpeedMultiplierMod  = 0.f;
	int64  ListenId            = 0;
	float  RunSpeed            = 0.f;
	float  SlopeModAngle       = 0.f;
	float  SlopeModPercent     = 0.f;
	float  TurnScale           = 0.f;
	float  WalkSpeed           = 0.f;
	float  WaterModPercent     = 0.f;
	TSWGBaselineList<FGroupMissionCriticalObject> SpaceMissionObjects;
	bool bHasBase4 = false;

	// ── Base6 (appended after Tangible6) ─────────────────────────────
	uint16 Level                  = 0;
	FString PerformanceAnimation;
	FString MoodString;
	int64  WeaponId               = 0;
	int64  GroupId                = 0;
	int64  GroupInviterId         = 0;
	int64  GroupInviteCounter     = 0;
	int32  GuildId                = 0;
	int64  TargetId                = 0;
	uint8  MoodId                  = 0;
	int32  PerformanceStartTime    = 0;
	int32  PerformanceType         = 0;
	TSWGBaselineList<int32> HAM;
	TSWGBaselineList<int32> MaxHAM;
	TSWGBaselineList<FEquiptmentItem> EquipmentList;
	FString AlternateAppearance;
	uint8  Frozen = 0;
	bool bHasBase6 = false;
};

namespace SWGCreatureBaselineParser
{
	SWGEMU_API void ParseBase1(FSWGPacket& Packet, FCreatureObjectBaseline& Out);
	SWGEMU_API void ParseBase3(FSWGPacket& Packet, FCreatureObjectBaseline& Out);
	SWGEMU_API void ParseBase4(FSWGPacket& Packet, FCreatureObjectBaseline& Out);
	SWGEMU_API void ParseBase6(FSWGPacket& Packet, FCreatureObjectBaseline& Out);
}
