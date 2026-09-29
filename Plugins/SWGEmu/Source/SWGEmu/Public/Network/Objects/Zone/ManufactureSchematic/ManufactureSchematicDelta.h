#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGDeltaListHelpers.h"

/**
 * Decoded updates from an MSCO delta message. Unlike the generic
 * DeltaVectorMap op table, base7's indexed list fields use the same
 * 1=add(index,value) and 2=set(index,value) wire operations as DeltaVector.
 * ReadDeltaVectorChanges handles their framing; each field supplies its own
 * element reader and retains the indexed changes for the crafting subsystem.
 */
struct SWGEMU_API FManufactureSchematicDelta
{
	// ── Base3 ──────────────────────────────────────────────────────
	TOptional<float> Complexity;
	TOptional<FString> Name;
	/** "Condition" per Core3's own field name — really the manufacture count remaining. */
	TOptional<int32> Condition;
	/** Field 5's list form (updateCraftingValues); the scalar form (updateManufactureLimit) exists in Core3 but has no callers — see crafting-protocol.md. */
	struct FCraftingValueUpdate { FString AttributeName; float Value = 0.f; };
	TOptional<TArray<FCraftingValueUpdate>> CraftingValues;

	// ── Base6 ──────────────────────────────────────────────────────
	/** insertToResourceSlot — the slot that just went from empty to filled. */
	TOptional<uint8> HighlightSlot;

	// ── Base7 ──────────────────────────────────────────────────────
	/** 0x08 initialAssemblyUpdate: fresh set of visible attribute group titles. */
	TOptional<TSWGListChanges<FString>> GroupTitles;
	/** 0x09 update9: current percentage per group, index-parallel with GroupTitles. */
	TOptional<TSWGListChanges<float>> GroupCurrentPercent;
	/** 0x0A update0A: defined but never sent by any Core3 caller — decoded for completeness only. */
	TOptional<TSWGListChanges<float>> GroupUnused0A;
	/** 0x0B update0B: "always 1.0" per Core3's own comment. */
	TOptional<TSWGListChanges<float>> GroupLockValue;
	/** 0x0C update0C: the max percentage a group can reach. */
	TOptional<TSWGListChanges<float>> GroupMaxPercent;
	/** 0x0D update0D: customization variable names, empty entries for non-ranged ones. */
	TOptional<TSWGListChanges<FString>> CustomizationVarNames;
	/** 0x0E update0E: each variable's starting/default value. */
	TOptional<TSWGListChanges<int32>> CustomizationVarDefaults;
	/** 0x0F update0F: always 0 per element; purpose unconfirmed (see crafting-protocol.md). */
	TOptional<TSWGListChanges<int32>> CustomizationVarUnused0F;
	/** 0x10 update10: palette colour count available, per Core3's comment on the base7 equivalent. */
	TOptional<TSWGListChanges<int32>> CustomizationPaletteCounts;
	/** 0x11 update11: customization-ready flag (scalar, always 1 when sent). */
	TOptional<uint8> CustomizationReady;
	/** 0x12 update12: experimentation failure rate for the assembly just performed. */
	TOptional<float> FailureRate;
	/** 0x13 update13: appearance template choices for the customize page. */
	TOptional<TSWGListChanges<FString>> Templates;
	/** 0x14 update14: defined but never sent by any Core3 caller — decoded for completeness only. */
	TOptional<uint8> Unused14;
};

namespace SWGManufactureSchematicDeltaParser
{
	SWGEMU_API void ParseDelta3(FSWGPacket& Packet, FManufactureSchematicDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta6(FSWGPacket& Packet, FManufactureSchematicDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta7(FSWGPacket& Packet, FManufactureSchematicDelta& Out, uint16 UpdateCount);
}
