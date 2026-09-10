#pragma once

#include "CoreMinimal.h"

struct FSWGPacket;

/** CombatManager's hit result codes, as they appear in a defender entry. */
enum class ESWGCombatHit : uint8
{
	Miss     = 0,
	Hit      = 1,
	Block    = 2,
	Dodge    = 3,
	Counter  = 4,
	Ricochet = 5,
};

/** One target of a swing, and how it went for them. */
struct SWGEMU_API FSWGCombatDefender
{
	int64 ObjectId = 0;

	/** The defender's posture as of the hit — an ESWGPosture value. */
	uint8 Posture = 0;

	/** An ESWGCombatHit. */
	uint8 Hit = 0;

	uint8 ClientEffectId = 0;

	/** Which body part was struck. Only present in the full form — see FCombatActionIn. */
	uint8 HitLocation = 0;

	/** Pre-mitigation damage, byte-clamped — a hit-reaction weight hint, not a
	 *  real number. The HAM delta is authoritative. Full form only. */
	uint8 InitialDamage = 0;
};

/**
 * One combat swing, broadcast to everyone in range (sub-opcode 0xCC).
 *
 * AnimationCrc hashes a name the server composes per swing
 * (CombatQueueCommand::getDefaultAttackAnimation, e.g.
 * "attack_mid_center_light_0"); FSWGCombatManagerReader resolves it back.
 *
 * Payload (Core3 CombatAction):
 *   animationCrc(int32) attackerId(int64) weaponId(int64)
 *   attackerPosture(byte) trails(byte) unused(byte)
 *   then, per defender: index(uint16) objectId(int64) posture(byte)
 *   hit(byte) clientEffectId(byte) [hitLocation(byte) initialDamage(byte)]
 *
 * That uint16 is a running 1-based index, not a count — Core3 re-emits it per
 * entry. The last two bytes only appear in the multi-defender constructor;
 * the TangibleObject-attacker path writes 11-byte entries. Neither is flagged,
 * so Parse infers the form from payload length.
 */
struct SWGEMU_API FCombatActionIn
{
	uint32 AnimationCrc = 0;
	int64  AttackerId = 0;
	int64  WeaponId = 0;

	/** The attacker's posture as of the swing — an ESWGPosture value. */
	uint8 AttackerPosture = 0;

	/** Weapon trail effect selector. 0xFF where the sending path has no opinion. */
	uint8 Trails = 0;

	TArray<FSWGCombatDefender> Defenders;

	bool Parse(FSWGPacket& Packet);

	/** True if the entries carried HitLocation/InitialDamage. */
	bool HasHitDetail() const { return bHasHitDetail; }

private:
	bool bHasHitDetail = false;
};
