#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"

/**
 * Decoded state for a TANO (Tangible Object) — item/prop-level fields shared
 * by every tangible, and by CreatureObject/PlayerObject which extend it.
 *
 * Field order and types verified against Core3's TangibleObjectMessage3/6
 * (server/zone/packets/tangible/). Delta field indices are positional —
 * confirmed against TangibleObjectDeltaMessage3 (0=Complexity, 1=ObjectName,
 * 2=CustomName, 4=CustomizationString, 6=OptionsBitmask, 7=UseCount,
 * 8=ConditionDamage, 9=MaxCondition — matching baseline declaration order).
 */
struct FTangibleObjectBaseline
{
	// ── Base3 ──────────────────────────────────────────────────────
	float          Complexity      = 0.f;
	FSWGStringId   ObjectName;
	FString        CustomName;
	int32          Volume          = 0;
	FString        CustomizationString;
	TSWGBaselineList<int32> VisibleComponents;
	int32          OptionsBitmask  = 0;
	int32          UseCount        = 0;
	int32          ConditionDamage = 0;
	int32          MaxCondition    = 0;
	uint8          ObjectVisible   = 0;
	bool           bHasBase3       = false;

	// ── Base6 ──────────────────────────────────────────────────────
	int32          Unknown076      = 0; // Fixed 0x76 for TANO, 0x3D for CREO — purpose unconfirmed server-side
	TSWGBaselineList<uint64> DefenderList;
	bool           bHasBase6       = false;
};

namespace SWGTangibleBaselineParser
{
	/** Parses the fields common to every tangible object's base3 slot. */
	SWGEMU_API void ParseBase3(FSWGPacket& Packet, FTangibleObjectBaseline& Out);

	/** Parses the fields common to every tangible object's base6 slot. */
	SWGEMU_API void ParseBase6(FSWGPacket& Packet, FTangibleObjectBaseline& Out);
}
