#pragma once

#include "CoreMinimal.h"

/**
 * Decodes the "customization" wire field Core3 sends in TANO/CREO baseline
 * data (field index 4, read via FSWGPacket::ReadAsciiBytes — NOT
 * ReadAsciiString, which corrupts bytes 0x80-0x9F by routing them through
 * the system codepage). This is the client-side mirror of Core3's
 * CustomizationVariables::getData()/parseFromClientString() (see
 * MMOCoreORB/src/server/zone/objects/scene/variables/CustomizationVariables.cpp):
 *
 *   [uint8 Unknown=1][uint8 Count]
 *   Count * { [uint8 TypeId] [uint8/int16 Value] }
 *   [0xFF 0x03]  (end-of-stream marker)
 *
 * Byte-escaped throughout (including inside Count/TypeId/Value, matching
 * the server's nextByte()): 0xFF is the escape lead byte — 0xFF 0x01 decodes
 * to a literal 0x00, 0xFF 0x02 decodes to a literal 0xFF, 0xFF 0x03 is only
 * valid as the final end-of-stream marker. A TypeId with its high bit
 * (0x80) set means Value is a full signed int16 (low byte first, then one
 * more escaped byte for the high byte); otherwise Value is 0-255.
 *
 * TypeId is a numeric key into a server-side name table
 * (CustomizationIdManager, loaded from an IFF the server keeps but doesn't
 * send to the client) — this decoder only recovers the raw (TypeId, Value)
 * pairs. Mapping TypeId to what it actually controls (a palette color index
 * via PaletteColorCustomizationVariable, or a plain ranged int) requires the
 * appearance template's own ACST customization-variable table, which this
 * client does not parse yet.
 */
struct SWGEMUCLIENT_API FSWGCustomizationVariables
{
	TMap<uint8, int16> Values;

	/**
	 * Parses RawBytes into OutResult.Values. Returns false (and clears
	 * OutResult.Values) on any malformed input — missing/misplaced
	 * end-of-stream marker, truncated escape sequence, etc. — mirroring
	 * Core3's own parseFromClientString, which discards everything on error.
	 * Empty RawBytes parses successfully to an empty Values map (Core3's
	 * getData() emits nothing at all for an object with no customization
	 * variables set).
	 */
	static bool Parse(const TArray<uint8>& RawBytes, FSWGCustomizationVariables& OutResult);
};
