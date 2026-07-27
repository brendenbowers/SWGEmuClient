#pragma once

#include "CoreMinimal.h"

/**
 * SWG's client/server-shared string CRC (Core3: String::hashCode(), a
 * classic table-driven CRC-32/BZIP2-style checksum — polynomial 0x04C11DB7,
 * MSB-first, seeded 0xFFFFFFFF, final value complemented). Every "TypeCrc"
 * SWG hashes from an asset path (object templates, appearance files, this
 * project's own MeshVirtualPath resolution) uses this exact algorithm — this
 * is currently the only place that needs to compute one locally rather than
 * just matching a CRC the server already sent, hence its own small utility
 * instead of duplicating a table inline wherever it's needed.
 */
class SWGTRE_API FSWGCrc32
{
public:
	static uint32 HashString(const FString& Value);
	static uint32 HashString(const ANSICHAR* Value);

private:
	static uint32 Table[256];
	static bool bTableInitialized;
	static void EnsureTable();
};
