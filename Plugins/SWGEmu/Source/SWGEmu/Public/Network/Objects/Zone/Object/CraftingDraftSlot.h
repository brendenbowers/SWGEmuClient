#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "CraftingDraftSlot.generated.h"

/**
 * One required ingredient slot of a draft schematic — Core3's DraftSlot,
 * always written by DraftSlot::insertToMessage
 * (templates/crafting/draftslot/DraftSlot.h) whether it's browsed
 * (0x1BF ResourceWeights' sibling, CraftingDraftSlotsIn) or read off an open
 * session's MSCO (0x103 CraftingIngredientSlotsIn) — identical wire shape
 * both places.
 *
 * Wire (DraftSlot::insertToMessage — the file/name pair really is written
 * twice, back to back, in every case; not a browsing-vs-session difference):
 *   ascii file, i32 0, ascii name, u8 optional, i32 1,
 *   ascii file(again), i32 0, ascii name(again),
 *   unicode resourceType,
 *   u8 kind, i32 quantity, [i16 0 if kind==Identical]
 */
UENUM(BlueprintType)
enum class ESWGDraftSlotKind : uint8
{
	None      = 0,  // never sent on the wire; only here so the reflected enum has a zero value
	Identical = 2,  // one item, or N identical stacked items
	Resource  = 4,  // a resource of ResourceType, up to Quantity units
	Mixed     = 5,  // a "component" slot: any of several different items/resources
};

USTRUCT(BlueprintType)
struct SWGEMU_API FSWGDraftSlot
{
	GENERATED_BODY()

	/** STF file/name naming the slot ("craft_food_ingredients_n" / "dried_fruit"). */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	FString StringIdFile;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	FString StringIdName;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	bool bOptional = false;

	/** Resource class ENUM this slot accepts ("organic"); empty for a component/identical slot. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	FString ResourceType;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	ESWGDraftSlotKind Kind = ESWGDraftSlotKind::Resource;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	int32 Quantity = 0;

	bool Deserialize(FSWGPacket& Packet)
	{
		StringIdFile = Packet.ReadAsciiString();
		Packet.ReadInt32(); // filler, always 0
		StringIdName = Packet.ReadAsciiString();
		bOptional = Packet.ReadByte() != 0;
		Packet.ReadInt32(); // constant 1
		Packet.ReadAsciiString(); // file, repeated
		Packet.ReadInt32(); // filler, repeated
		Packet.ReadAsciiString(); // name, repeated
		ResourceType = Packet.ReadUnicodeString();
		Kind = static_cast<ESWGDraftSlotKind>(Packet.ReadByte());
		Quantity = Packet.ReadInt32();
		if (Kind == ESWGDraftSlotKind::Identical)
		{
			Packet.ReadInt16(); // terminator, identical slots only
		}
		return !Packet.IsError();
	}
};

/**
 * One resource attribute's crafting weight — Core3's ResourceWeight
 * (templates/crafting/resourceweight/ResourceWeight.h). A schematic sends two
 * parallel lists of these in 0x207 (CraftingResourceWeightsIn): a "batch"
 * form whose weight nibble is always 1 (grouping only), and the real
 * per-attribute weights.
 *
 * Wire: u8 count, [count x u8 (property<<4 | weight)]. count==0 covers both
 * "no properties" and Core3's own "first byte is 0" empty check.
 */
USTRUCT(BlueprintType)
struct SWGEMU_API FSWGResourceWeightEntry
{
	GENERATED_BODY()

	/** PO=0 CR=1 CD=2 DR=3 HR=4 FL=5 MA=6 PE=7 OQ=8 SR=9 UT=10 (crafting.stf res_* captions). */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	uint8 Property = 0;

	/** 1-15; the batch form always sends 1 here regardless of the real weight. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	uint8 Weight = 0;
};

USTRUCT(BlueprintType)
struct SWGEMU_API FSWGResourceWeight
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	TArray<FSWGResourceWeightEntry> Entries;

	bool Deserialize(FSWGPacket& Packet)
	{
		const uint8 Count = Packet.ReadByte();
		Entries.Reset(Count);
		for (uint8 Index = 0; Index < Count && !Packet.IsError(); ++Index)
		{
			const uint8 TypeAndWeight = Packet.ReadByte();
			FSWGResourceWeightEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Property = TypeAndWeight >> 4;
			Entry.Weight = TypeAndWeight & 0x0F;
		}
		return !Packet.IsError();
	}
};
