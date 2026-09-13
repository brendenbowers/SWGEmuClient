#pragma once

#include "CoreMinimal.h"

/**
 * One decoded string/<lang>/<table>.stf — the localized text behind every
 * "@table:key" reference (object names, NPC names, sign text, system
 * messages). Keys are the ascii ids the wire/templates use ("bantha"),
 * values are the UTF-16 display text ("a bantha").
 */
struct SWGTRE_API FSWGStringTable
{
	TMap<FString, FString> Entries;

	const FString* Find(const FString& Key) const { return Entries.Find(Key); }
};

/**
 * Decoder for the .stf binary layout (not IFF), confirmed by hex dump of
 * string/en/mob/creature_names.stf:
 *
 *   u32 magic 0x0000ABCD, u8 flags, u32 nextUniqueId, u32 count
 *   count x { u32 id, u32 crc (0xFFFFFFFF), u32 len, UTF-16LE[len] }   values
 *   count x { u32 id, u32 len, ascii[len] }                            keys
 *
 * Values and keys are paired by id, not by position.
 */
class SWGTRE_API FSWGStringTableReader
{
public:
	static bool Read(const TArray<uint8>& Data, FSWGStringTable& OutTable);

	/**
	 * Splits "@table:key" (or "table:key") into its parts. Returns false for
	 * anything without a ':' — a literal string rather than a reference.
	 */
	static bool ParseStringId(const FString& Reference, FString& OutTable, FString& OutKey);

private:
	FSWGStringTableReader() = default;
};
