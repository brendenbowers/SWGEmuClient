#pragma once

#include "CoreMinimal.h"

/**
 * Converts a 4 character string to the unit used by packets to denote an object type
 */
constexpr uint32 SWGFourCC(const ANSICHAR (&Str)[5])
{
	return (static_cast<uint32>(static_cast<uint8>(Str[0])) << 24)
		 | (static_cast<uint32>(static_cast<uint8>(Str[1])) << 16)
		 | (static_cast<uint32>(static_cast<uint8>(Str[2])) << 8)
		 |  static_cast<uint32>(static_cast<uint8>(Str[3]));
}

/**
 * The object types that appear as ObjectType in Baselines/Deltas messages, so
 * dispatch can switch on them instead of comparing loose constants:
 *
 *   switch (Msg.GetObjectType())
 *   {
 *       case ESWGObjectType::TANO:
 *       case ESWGObjectType::WEAO: ...
 *   }
 *
 * Values are derived from the FourCC text itself, so adding a type is one line
 * and can't be given a wrong number.
 */
enum class ESWGObjectType : uint32
{
	Invalid = 0,

	CREO = SWGFourCC("CREO"),  // Creature (NPC/player body)
	PLAY = SWGFourCC("PLAY"),  // Player object (the CREO's player-only companion)
	TANO = SWGFourCC("TANO"),  // Tangible (item)
	WEAO = SWGFourCC("WEAO"),  // Weapon — TANO-derived baseline layout
	RCNO = SWGFourCC("RCNO"),  // Resource container
	ITNO = SWGFourCC("ITNO"),  // Intangible (datapad contents, pets in storage)
	STAO = SWGFourCC("STAO"),  // Static (prop)
	BUIO = SWGFourCC("BUIO"),  // Building
	HINO = SWGFourCC("HINO"),  // Harvester/house installation
	INSO = SWGFourCC("INSO"),  // Installation (factory/harvester)
	SCLT = SWGFourCC("SCLT"),  // Cell (building interior)
	FCYT = SWGFourCC("FCYT"),  // Factory crate
	MSCO = SWGFourCC("MSCO"),  // Manufacture schematic
	MISO = SWGFourCC("MISO"),  // Mission
	WAYP = SWGFourCC("WAYP"),  // Waypoint
	GRUP = SWGFourCC("GRUP"),  // Group
	GILD = SWGFourCC("GILD"),  // Guild
};

/** Decode an object type back into its 4-character ASCII form (e.g. "CREO"), for logging. */
SWGEMU_API FString SWGFourCCToString(uint32 FourCC);

inline FString SWGFourCCToString(ESWGObjectType ObjectType)
{
	return SWGFourCCToString(static_cast<uint32>(ObjectType));
}
