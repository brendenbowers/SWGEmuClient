#pragma once

#include "CoreMinimal.h"

class FSWGIffReader;

/**
 * What one combat action asks the two participants to play.
 *
 * These are *action* names, not logical animations — the performer's own .ash
 * FORM ACTS turns them into something the .lat can resolve, per weapon and
 * posture. See FSWGAshReader.
 */
struct FSWGCombatActionAnimation
{
	/** Playback script the original client would run, e.g. "playback/play_2_face.pst". */
	FString PlaybackScript;

	/** The attacker's action, from the entry's TCAA variable, e.g. "attack_mid_center_0". */
	FString AttackerAction;

	/** The defender's reaction, from CAHD, e.g. "get_hit_light_mid_center". Empty when the defender plays nothing. */
	FString DefenderAction;

	bool IsValid() const { return !AttackerAction.IsEmpty() || !DefenderAction.IsEmpty(); }
};

/**
 * One combat_manager entry: the animation for a given hit result and defender
 * posture. The file nests those as two dispatches — hit result outside,
 * posture inside, 0xFFFF the default arm of each — flattened here into one
 * lookup, since neither is useful without the other.
 */
struct SWGANIMATION_API FSWGCombatManagerEntry
{
	/** The server-side animation name this entry is keyed by, e.g. "attack_mid_center_light_0". */
	FString Key;

	/** Hit result -> defender posture -> animation. Key 0xFFFF is the default arm at both levels. */
	TMap<uint16, TMap<uint16, FSWGCombatActionAnimation>> ByHitThenPosture;

	/** The default arm at both levels. Present for entries the file writes as a single SNGL with no dispatch at all. */
	static constexpr uint16 DefaultArm = 0xFFFF;

	/**
	 * The animation for a hit result and defender posture, falling back to
	 * each level's default arm. Null when the entry has nothing for either.
	 */
	const FSWGCombatActionAnimation* Resolve(uint8 HitResult, uint8 DefenderPosture) const;
};

/**
 * A decoded combat/combat_manager.iff — the hop between CombatAction and the
 * .ash/.lat chain. Only the hash of the server's name reaches us, so the
 * reverse mapping is built by hashing every key at load.
 */
struct SWGANIMATION_API FSWGCombatManagerData
{
	TArray<FSWGCombatManagerEntry> Entries;

	/** FSWGCrc32::HashString(Key) -> index into Entries. How an inbound CombatAction is resolved. */
	TMap<uint32, int32> ByKeyCrc;

	const FSWGCombatManagerEntry* FindByCrc(uint32 Crc) const
	{
		const int32* Index = ByKeyCrc.Find(Crc);
		return Index ? &Entries[*Index] : nullptr;
	}

	const FSWGCombatManagerEntry* FindByName(const FString& Key) const;
};

/**
 * Parses combat/combat_manager.iff: FORM CBTM > FORM 0002 > one FORM ENTR per
 * action name. Each ENTR holds a KEY chunk (the name) and then either a FORM
 * SNGL directly, or a FORM DDSP of hit-result arms, each of which is either a
 * SNGL or a FORM DEPS of defender-posture arms.
 *
 * A SNGL is a NAME chunk (the .pst path plus one trailing byte) and a FORM
 * VARS of STRN chunks. Each STRN is a 4-character variable name immediately
 * followed by its null-terminated value, no separator — TCAA is the attacker's
 * action, CAHD the defender's.
 *
 * The file sits at the archive root, not under datatables/, which is why a
 * datatable sweep does not find it.
 */
class SWGANIMATION_API FSWGCombatManagerReader
{
public:
	static bool ReadCombatManager(const FSWGIffReader& Reader, FSWGCombatManagerData& OutData);

private:
	FSWGCombatManagerReader() = default;
};
