#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/TangibleObjectDelta.h"
#include "Network/Objects/Zone/Creature/EquiptmentItem.h"
#include "Network/Objects/Zone/Creature/SkillModifier.h"
#include "Network/Objects/Zone/Creature/GroupMissionCriticalObject.h"

/** Decoded updates from a CREO delta slot. Slots 3 and 6 extend the tangible ones. */
struct SWGEMU_API FCreatureObjectDelta
{
	FTangibleObjectDelta Tangible;

	// ── Base1 ──────────────────────────────────────────────────────
	TOptional<int32>         BankCredits;
	TOptional<int32>         CashCredits;
	TSWGListChanges<int32>   BaseHAM;
	TSWGListChanges<FString> SkillList;

	// ── Base3 ──────────────────────────────────────────────────────
	TOptional<int32>       IncapacitationRecoveryTime;
	TOptional<uint8>       Posture;
	TOptional<uint8>       FactionRank;
	TOptional<int64>       CreatureLinkId;
	TOptional<float>       Height;
	TOptional<int32>       ShockWounds;
	TOptional<int64>       StateBitmask;
	TSWGListChanges<int32> Wounds;

	// ── Base4 ──────────────────────────────────────────────────────
	TOptional<float>                          AccelerationMultiplierBase;
	TOptional<float>                          AccelerationMultiplierMod;
	TSWGListChanges<int32>                    Encumbrances;
	TSWGListChanges<FSkillModifier>           SkillMods;
	TOptional<float>                          SpeedMultiplierBase;
	TOptional<float>                          SpeedMultiplierMod;
	TOptional<int64>                          ListenId;
	TOptional<float>                          RunSpeed;
	TOptional<float>                          SlopeModAngle;
	TOptional<float>                          SlopeModPercent;
	TOptional<float>                          TurnScale;
	TOptional<float>                          WalkSpeed;
	TOptional<float>                          WaterModPercent;
	TSWGListChanges<FGroupMissionCriticalObject> SpaceMissionObjects;

	// ── Base6 ──────────────────────────────────────────────────────
	TOptional<uint16>              Level;
	TOptional<FString>             PerformanceAnimation;
	TOptional<FString>             MoodString;
	TOptional<int64>               WeaponId;
	TOptional<int64>               GroupId;
	TOptional<int64>               GroupInviterId;
	TOptional<int64>               GroupInviteCounter;
	TOptional<int32>               GuildId;
	TOptional<int64>               TargetId;
	TOptional<uint8>               MoodId;
	TOptional<int32>               PerformanceStartTime;
	TOptional<int32>               PerformanceType;
	TSWGListChanges<int32>         HAM;
	TSWGListChanges<int32>         MaxHAM;
	TSWGListChanges<FEquiptmentItem> EquipmentList;
	TOptional<FString>             AlternateAppearance;
};

namespace SWGCreatureDeltaParser
{
	SWGEMU_API void ParseDelta1(FSWGPacket& Packet, FCreatureObjectDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta3(FSWGPacket& Packet, FCreatureObjectDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta4(FSWGPacket& Packet, FCreatureObjectDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta6(FSWGPacket& Packet, FCreatureObjectDelta& Out, uint16 UpdateCount);
}
