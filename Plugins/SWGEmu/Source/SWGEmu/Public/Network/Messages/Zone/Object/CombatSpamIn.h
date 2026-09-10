#pragma once

#include "CoreMinimal.h"

struct FSWGPacket;

/** Core3's colour flag for a combat log line. */
enum class ESWGCombatSpamColor : uint8
{
	White = 0,
	/** Green when we dealt it, red when we took it — the client decides. */
	Auto  = 1,
	Red   = 10,
	Yellow = 11,
};

/**
 * One line of the combat log (sub-opcode 0x134).
 *
 * Two shapes share the field sequence, so which arrived is a matter of which
 * fields are non-empty: a stringfile reference whose text takes substitutions
 * (%TU attacker, %TT defender, %TO item, %DI damage), or a line Core3 composed
 * itself, which fills CustomText and leaves the ids and stringfile empty.
 *
 * Payload (Core3 CombatSpam):
 *   attackerId(int64) defenderId(int64) itemId(int64) damage(int32)
 *   stringFile(ascii) padding(int32) stringName(ascii) color(byte)
 *   customText(unicode)
 */
struct SWGEMU_API FCombatSpamIn
{
	int64 AttackerId = 0;
	int64 DefenderId = 0;
	int64 ItemId = 0;

	int32 Damage = 0;

	/** Stringfile the line's text lives in, e.g. "cbt_spam". Empty on a custom line. */
	FString StringFile;

	/** Entry within StringFile, e.g. "attack_hit". Empty on a custom line. */
	FString StringName;

	/** An ESWGCombatSpamColor. */
	uint8 Color = 0;

	/** Fully composed text, for lines that carry their own. Empty otherwise. */
	FString CustomText;

	bool Parse(FSWGPacket& Packet);

	/** "@file:name" — the stringfile reference in the form the rest of SWG spells it. */
	FString GetStringId() const;
};
