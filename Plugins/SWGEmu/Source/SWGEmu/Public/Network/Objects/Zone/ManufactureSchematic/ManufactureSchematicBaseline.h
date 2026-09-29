#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"

/**
 * Decoded state for an MSCO (ManufactureSchematic) — the item under
 * construction in an open crafting session. Base0-2 carry nothing crafting
 * needs (generic SceneObject fields); base8/9 are sent but always empty
 * (ManufactureSchematicObjectMessage8/9.h — operand count 0, no payload).
 *
 * Source: server/zone/objects/manufactureschematic/ManufactureSchematic.idl
 * (field list) and ManufactureSchematicImplementation.cpp (wire order) —
 * see Plugins/SWGEmu/crafting-protocol.md for the full field-by-field trace.
 */
struct SWGEMU_API FManufactureSchematicBaseline
{
	// ── Base3 ──────────────────────────────────────────────────────
	float Complexity = 0.f;
	FString ObjectNameFile;
	FString ObjectNameName;
	/** The player-set custom name, if any (e.g. after 0x15A customize). */
	FString CustomName;
	/** getDataSize() is a float server-side but Core3 writes it via insertInt (ManufactureSchematicObjectMessage3.h) — read as the truncated int32 it actually is on the wire. */
	int32 DataSize = 0;
	int32 ManufactureLimit = 0;
	/** The crafter's own first name, as sent back to them. */
	FString PlayerName;
	bool bHasBase3 = false;

	// ── Base6 ──────────────────────────────────────────────────────
	uint32 SchematicCrc = 0;
	/** True while this schematic is the one an open session is actively crafting. */
	bool bActiveCraft = false;
	bool bHasBase6 = false;

	// ── Base7 ──────────────────────────────────────────────────────
	// Per-slot arrays, all the same length (draftSchematic->getDraftSlotCount()):
	/** Slot display names (STF file+name), index-parallel with the rest. */
	TSWGBaselineList<FSWGStringId> IngredientNames;
	/** Per-slot type; 0 until something is placed (DraftSlot::slotType once filled — unconfirmed exact semantics, see crafting-protocol.md open items). */
	TSWGBaselineList<int32> IngredientTypes;
	/**
	 * Per-slot list of ingredient object ids currently placed there.
	 * Uint64VectorDeltaVector = DeltaVector<Vector<uint64>>: the OUTER
	 * count/updateCounter/items shape is DeltaVector's own (confirmed from
	 * Core3's DeltaVector.h source); each inner Vector<uint64> is read as a
	 * plain count-prefixed array (int32 count + items, no counter) — that
	 * inner shape is NOT independently source-verified (Vector<T>'s message
	 * serialization is engine3 core, not vendored in this checkout) but
	 * matches every other bare-list convention seen throughout Core3's own
	 * code. Flagged in crafting-protocol.md item 5 — recheck against a real
	 * capture before trusting a slot with more than one ingredient id.
	 */
	TSWGBaselineList<TArray<uint64>> SlotOIDs;
	/** Per-slot list of quantities placed, index-parallel with SlotOIDs. Same inner-shape caveat as SlotOIDs. */
	TSWGBaselineList<TArray<int32>> SlotQuantities;
	/** Per-slot average resource quality, 0 until something is placed. */
	TSWGBaselineList<float> SlotQualities;
	/** Per-slot "clean" flag; initializeIngredientSlots sets 0xFFFFFFFF (as int32 -1) before anything is placed. */
	TSWGBaselineList<int32> SlotClean;
	/** 0..N-1, one per slot — the slot's own index, for correlating with FSWGDraftSlot's order. */
	TSWGBaselineList<int32> SlotIndexes;
	uint8 IngredientCounter = 0;

	// Experimentation display (visible attribute groups):
	TArray<FString> ExperimentGroupTitles;
	TArray<float> ExperimentCurrentPercent;
	/** Always 0 per Core3's own comment ("useless values"); kept for completeness. */
	TArray<int32> ExperimentOffsets;
	/** Always 1.0 per Core3's own comment ("always 1"). */
	TArray<float> ExperimentMax;

	uint8 CustomizationCounter = 0;
	float RiskFactor = 0.f;
	bool bReady = false;
	bool bHasBase7 = false;
};

namespace SWGManufactureSchematicBaselineParser
{
	SWGEMU_API void ParseBase3(FSWGPacket& Packet, FManufactureSchematicBaseline& Out);
	SWGEMU_API void ParseBase6(FSWGPacket& Packet, FManufactureSchematicBaseline& Out);
	SWGEMU_API void ParseBase7(FSWGPacket& Packet, FManufactureSchematicBaseline& Out);
}
