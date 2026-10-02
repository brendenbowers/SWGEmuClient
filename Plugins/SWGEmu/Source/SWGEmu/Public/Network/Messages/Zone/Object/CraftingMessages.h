#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/Zone/ObjectControllerMessage.h"
#include "Network/Objects/Zone/Object/CraftingDraftSlot.h"
#include "CraftingMessages.generated.h"

struct FSWGPacket;

// ── Server → client ───────────────────────────────────────────────────────
// All payloads here start right after the ObjController envelope
// (Priority/Type/ObjectId) — parse from FObjControllerMessageIn::AsPayloadPacket().

/** One schematic a session's tool/station offers, from the 0x102 list. */
USTRUCT(BlueprintType)
struct SWGEMU_API FSWGCraftingSchematicEntry
{
	GENERATED_BODY()

	/** DraftSchematic's clientObjectCRC — matches FDraftSchematic::SchematicCRC on PLAY base9. int32, not uint32: uint32 isn't Blueprint-exposable. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	int32 SchematicCrc = 0;

	/** Bitmask matching one of the crafting tool's enabledTabs values. int32, not uint32: uint32 isn't Blueprint-exposable. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	int32 ToolTab = 0;
};

/**
 * A crafting session just started (sub-op 0x102) — the schematics this
 * tool/station combination offers. Sent once, right after
 * "requestCraftingSession" succeeds.
 *
 * Wire (CraftingSessionImplementation::startSession):
 *   u64 toolId, u64 stationId, i32 count, [u32 crc, u32 crc(repeat), u32 toolTab]*
 */
struct SWGEMU_API FSWGCraftingSchematicListIn
{
	uint64 ToolId = 0;
	uint64 StationId = 0;
	TArray<FSWGCraftingSchematicEntry> Schematics;

	bool Parse(FSWGPacket& Packet);
};

/**
 * A draft schematic's required ingredient slots, either from browsing
 * ("requestDraftSlotsBatch", sub-op 0x1BF, no session needed) or from an
 * open session's MSCO right after picking it (sub-op 0x103, below) — same
 * FSWGDraftSlot shape either way.
 *
 * Wire (DraftSchematicImplementation::sendDraftSlotsTo/insertIngredients):
 *   u32 crc, u32 crc(repeat), i32 complexity, i32 size, u8 2(const),
 *   i32 slotCount, [DraftSlot]*, i16 0(terminator)
 */
struct SWGEMU_API FSWGCraftingDraftSlotsIn
{
	uint32 SchematicCrc = 0;
	int32 Complexity = 0;
	int32 Size = 0;
	TArray<FSWGDraftSlot> Slots;

	bool Parse(FSWGPacket& Packet);
};

/**
 * A draft schematic's per-attribute resource weights, from browsing
 * ("requestResourceWeightsBatch", sub-op 0x207, no session needed).
 *
 * Wire (DraftSchematicImplementation::sendResourceWeightsTo): two parallel
 * lists of the same attributes — BatchWeights groups them (weight forced to
 * 1), Weights carries the real weight used to combine resource properties.
 *   u32 crc, u32 crc(repeat), u8 n, [ResourceWeight]*n (batch form),
 *   u8 n(repeat), [ResourceWeight]*n (real form)
 */
struct SWGEMU_API FSWGCraftingResourceWeightsIn
{
	uint32 SchematicCrc = 0;
	TArray<FSWGResourceWeight> BatchWeights;
	TArray<FSWGResourceWeight> Weights;

	bool Parse(FSWGPacket& Packet);
};

/**
 * An open session's MSCO ingredient slots for the UI (sub-op 0x103), sent
 * after the client asks with "synchronizedUiListen" (target = the MSCO).
 * The MSCO's own base7 (ManufactureSchematicBaseline.h) carries what's
 * already placed in each slot; this carries what's required.
 *
 * Wire (CraftingSessionImplementation::sendIngredientForUIListen):
 *   u64 toolId, u64 mscoId, u64 prototypeId, i32 2(const), u8 allowFactory,
 *   i32 slotCount, [DraftSlot]*, i16 0(terminator)
 */
struct SWGEMU_API FSWGCraftingIngredientSlotsIn
{
	uint64 ToolId = 0;
	uint64 ManufactureSchematicId = 0;
	uint64 PrototypeId = 0;
	uint8 AllowFactory = 0;
	TArray<FSWGDraftSlot> Slots;

	bool Parse(FSWGPacket& Packet);
};

/** Core3 IngredientSlot::* error codes (ingredientslots/IngredientSlot.h:36-63) — Value on a 0x10C AddIngredientResult/RemoveIngredient reply, and the return of the session's slot ops in general. */
UENUM(BlueprintType)
enum class ESWGCraftingSlotResult : uint8
{
	OK                       = 0x00,
	NoServer                 = 0x01,
	NotAssemblyStage         = 0x02,
	NotCustomizationStage    = 0x03,
	NoSchematic              = 0x04,
	NoTool                   = 0x05,
	NoManufacture            = 0x06,
	Invalid                  = 0x07,
	InvalidOption            = 0x08,
	InvalidIngredientSize    = 0x09,
	Full                     = 0x0A,
	InvalidIngredient        = 0x0B,
	IngredientNotInInventory = 0x0C,
	BadCrate                 = 0x0D,
	BadResourceFor           = 0x0E,
	ComponentDamaged         = 0x0F,
	NoComponentTransfer      = 0x10,
	BadComponent             = 0x11,
	NoInventory              = 0x12,
	BadStationHopper         = 0x13,
	BadTargetContainer       = 0x14,
	EmptyIsEmpty             = 0x15,
	FailedResourceCreate     = 0x16,
	EmptyAssemble            = 0x17,
	PartialAssemble          = 0x18,
	PrototypeNotFound        = 0x19,
	BadName                  = 0x1A,
	Mystery                  = 0x1B,
	FailedToTransfer         = 0x1C,
	WeirdFailedMessage       = 0x1D,
};

/** Core3 CraftingManager::* assembly/experiment result codes (CraftingManager.idl:37-45). */
UENUM(BlueprintType)
enum class ESWGCraftingResult : uint8
{
	AmazingSuccess    = 0,
	GreatSuccess      = 1,
	GoodSuccess       = 2,
	ModerateSuccess   = 3,
	Success           = 4,
	MarginalSuccess   = 5,
	Ok                = 6,
	BarelySuccessful  = 7,
	CriticalFailure   = 8,
};

/**
 * The shared i32/i32/u8 shape behind three different sub-ops — Core3 sends
 * exactly this layout for all of them, so one struct covers all three; only
 * SubType's meaning differs by which one arrived (check
 * FObjControllerMessageIn::GetSubOp(), not this struct, to tell them apart):
 *
 *  - 0x10C CraftingSlotReply: SubType picks the reply's meaning (a
 *    IngredientSlot result code for 0x107/@0x15A, or a fixed marker for the
 *    hopper/failure replies) — see ESWGCraftingSlotReplySubType. Value is
 *    the IngredientSlot::* result code when SubType==0x107, else a small
 *    fixed constant (0/1) whose meaning is per-SubType.
 *  - 0x1BE CraftingAssemblyResult: SubType is always the literal 0x109;
 *    Value is a CraftingManager assembly result (ESWGCraftingResult) on an
 *    initial assemble, or the literal state 4 when just finishing without
 *    assembling.
 *  - 0x113 CraftingExperimentResult: SubType is always the literal 0x105;
 *    Value is a CraftingManager result (ESWGCraftingResult).
 *
 * Wire: i32 subType, i32 value, u8 counter.
 */
struct SWGEMU_API FSWGCraftingStatusIn
{
	int32 SubType = 0;
	int32 Value = 0;
	uint8 Counter = 0;

	bool Parse(FSWGPacket& Packet);
};

/**
 * SubType values seen on 0x10C (CraftingSessionImplementation.cpp /
 * CraftingToolImplementation.cpp). Plain C++ enum, not UENUM: Blueprint
 * enums must be uint8-backed and these wire values (0x107 etc.) don't fit
 * a byte.
 */
enum class ESWGCraftingSlotReplySubType : int32
{
	Unknown                = 0x000, // never sent on the wire; only here so the reflected enum has a zero value
	AddIngredientResult    = 0x107, // Value = IngredientSlot::* result code, reply to our 0x107
	RemoveIngredientOk     = 0x108, // Value always 0, reply to a successful 0x108
	HopperBracket          = 0x10A, // Value 1 then 0 (two separate sends) around a successful create; also 1 alone for createManfSchematic
	ToolStartFailed        = 0x10F, // Value always 0; a system-message string follows separately
	CustomizeApplied       = 0x15A, // Value always 0, reply to our 0x15A
};

/** The window closed (sub-op 0x1C2) — the session is over on the server. Wire: u8 counter. */
struct SWGEMU_API FSWGCraftingCloseWindowIn
{
	uint8 Counter = 0;

	bool Parse(FSWGPacket& Packet);
};

// ── Client → server ──────────────────────────────────────────────────────
// All Serialize() below wrap the payload in the same ObjController envelope
// ObjectMenuRequest/CommandQueueEnqueue/TargetUpdate use: priority 0x0B,
// ObjectId = the local player's own object id, SerializeBase() writes
// Core3's 0x05 operand count.

/** One row's spend in a CraftingExperiment message. */
USTRUCT(BlueprintType)
struct SWGEMU_API FSWGCraftingExperimentRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	int32 RowIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	int32 Points = 0;
};

/**
 * Spends experimentation points on one or more rows (sub-op 0x106). Only
 * valid at session state 3 (CraftingExperimentCallback::run).
 *
 * Wire: u32 0(size, ignored), u8 counter, i32 rowCount, [i32 row, i32 points]*
 */
struct SWGEMU_API FSWGCraftingExperimentMessage : public FObjectControllerMessage
{
	uint8 Counter = 0;
	TArray<FSWGCraftingExperimentRow> Rows;

	FSWGCraftingExperimentMessage(uint64 PlayerId, uint8 Counter, const TArray<FSWGCraftingExperimentRow>& Rows);
	FSWGPacket Serialize() const;
};

/**
 * Puts an item/resource in a slot (sub-op 0x107). Valid at session state <= 2.
 *
 * Wire (CraftingAddIngredientCallback::parse):
 *   u32 0(size, ignored), u64 ingredientObjectId, i32 slot, u32 0(unused), u8 counter
 */
struct SWGEMU_API FSWGCraftingAddIngredientMessage : public FObjectControllerMessage
{
	uint64 IngredientObjectId = 0;
	int32 Slot = 0;
	uint8 Counter = 0;

	FSWGCraftingAddIngredientMessage(uint64 PlayerId, uint64 IngredientObjectId, int32 Slot, uint8 Counter);
	FSWGPacket Serialize() const;
};

/**
 * Takes an item/resource back out of a slot (sub-op 0x108).
 *
 * Wire (CraftingRemoveIngredientCallback::parse) — note the field order is
 * NOT the same as 0x107 (slot comes before the object id here):
 *   u32 0(size, ignored), i32 slot, u64 ingredientObjectId, u8 counter
 */
struct SWGEMU_API FSWGCraftingRemoveIngredientMessage : public FObjectControllerMessage
{
	int32 Slot = 0;
	uint64 IngredientObjectId = 0;
	uint8 Counter = 0;

	FSWGCraftingRemoveIngredientMessage(uint64 PlayerId, int32 Slot, uint64 IngredientObjectId, uint8 Counter);
	FSWGPacket Serialize() const;
};

/** One appearance customization variable's new value. */
USTRUCT(BlueprintType)
struct SWGEMU_API FSWGCraftingCustomizationEdit
{
	GENERATED_BODY()

	/** Index into the variable list MSCO7 update 0x0D sent. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	int32 Index = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	int32 Value = 0;
};

/**
 * Names the item, optionally picks an appearance template, and sets colour
 * variables (sub-op 0x15A). Valid any time after assembly (Core3 doesn't
 * gate it to a specific state beyond requiring a manufacture schematic and
 * prototype to exist).
 *
 * Wire (CraftingCustomizationCallback::parse):
 *   u32 0(size, ignored), unicode name, u8 templateChoice(0xFF = none),
 *   i32 schematicCount, u8 editCount, [i32 index, i32 value]*
 */
struct SWGEMU_API FSWGCraftingCustomizeMessage : public FObjectControllerMessage
{
	FString Name;
	uint8 TemplateChoice = 0xFF;
	int32 SchematicCount = 0;
	TArray<FSWGCraftingCustomizationEdit> Edits;

	FSWGCraftingCustomizeMessage(uint64 PlayerId, const FString& Name, uint8 TemplateChoice,
		int32 SchematicCount, const TArray<FSWGCraftingCustomizationEdit>& Edits);
	FSWGPacket Serialize() const;
};
