#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/Object/CraftingMessages.h"

namespace
{
	/** Writes one DraftSlot exactly as Core3's DraftSlot::insertToMessage does — the file/name pair twice, then the kind-dependent tail. */
	void WriteDraftSlot(FSWGPacket& Pkt, const FString& File, const FString& Name, bool bOptional,
		const FString& ResourceType, ESWGDraftSlotKind Kind, int32 Quantity)
	{
		Pkt.WriteAsciiString(File);
		Pkt.WriteInt32(0);
		Pkt.WriteAsciiString(Name);
		Pkt.WriteByte(bOptional ? 1 : 0);
		Pkt.WriteInt32(1);
		Pkt.WriteAsciiString(File);
		Pkt.WriteInt32(0);
		Pkt.WriteAsciiString(Name);
		Pkt.WriteUnicodeString(ResourceType);
		Pkt.WriteByte(static_cast<uint8>(Kind));
		Pkt.WriteInt32(Quantity);
		if (Kind == ESWGDraftSlotKind::Identical)
		{
			Pkt.WriteInt16(0);
		}
	}

	void WriteResourceWeight(FSWGPacket& Pkt, TArrayView<const TPair<uint8, uint8>> Entries)
	{
		Pkt.WriteByte(static_cast<uint8>(Entries.Num()));
		for (const TPair<uint8, uint8>& Entry : Entries)
		{
			Pkt.WriteByte((Entry.Key << 4) | (Entry.Value & 0x0F));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGCraftingSchematicListParseTest,
	"SWGEmu.Messages.Crafting.SchematicList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGCraftingSchematicListParseTest::RunTest(const FString& Parameters)
{
	FSWGPacket Pkt;
	Pkt.WriteUInt64(1001); // toolId
	Pkt.WriteUInt64(2002); // stationId
	Pkt.WriteInt32(2);
	Pkt.WriteUInt32(0xAABBCCDD); Pkt.WriteUInt32(0xAABBCCDD); Pkt.WriteUInt32(4); // generic tool tab
	Pkt.WriteUInt32(0x11223344); Pkt.WriteUInt32(0x11223344); Pkt.WriteUInt32(64);
	Pkt.SetReadMode();

	FSWGCraftingSchematicListIn List;
	if (TestTrue(TEXT("parses"), List.Parse(Pkt)))
	{
		TestEqual(TEXT("toolId"), List.ToolId, 1001ull);
		TestEqual(TEXT("stationId"), List.StationId, 2002ull);
		if (TestEqual(TEXT("count"), List.Schematics.Num(), 2))
		{
			TestEqual(TEXT("crc0"), List.Schematics[0].SchematicCrc, static_cast<int32>(0xAABBCCDDu));
			TestEqual(TEXT("tab1"), List.Schematics[1].ToolTab, 64);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGCraftingDraftSlotsParseTest,
	"SWGEmu.Messages.Crafting.DraftSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGCraftingDraftSlotsParseTest::RunTest(const FString& Parameters)
{
	FSWGPacket Pkt;
	Pkt.WriteUInt32(0x5000); Pkt.WriteUInt32(0x5000);
	Pkt.WriteInt32(3);   // complexity
	Pkt.WriteInt32(1);   // size
	Pkt.WriteByte(2);    // constant
	Pkt.WriteInt32(2);   // slot count
	WriteDraftSlot(Pkt, TEXT("craft_food_ingredients_n"), TEXT("dried_fruit"), false, TEXT("organic"), ESWGDraftSlotKind::Resource, 3);
	WriteDraftSlot(Pkt, TEXT("craft_food_ingredients_n"), TEXT("flavoring"), true, TEXT(""), ESWGDraftSlotKind::Identical, 1);
	Pkt.WriteInt16(0); // terminator
	Pkt.SetReadMode();

	FSWGCraftingDraftSlotsIn Slots;
	if (TestTrue(TEXT("parses"), Slots.Parse(Pkt)))
	{
		TestEqual(TEXT("crc"), Slots.SchematicCrc, 0x5000u);
		TestEqual(TEXT("complexity"), Slots.Complexity, 3);
		if (TestEqual(TEXT("slot count"), Slots.Slots.Num(), 2))
		{
			TestEqual(TEXT("slot0 name"), Slots.Slots[0].StringIdName, FString(TEXT("dried_fruit")));
			TestEqual(TEXT("slot0 resource type"), Slots.Slots[0].ResourceType, FString(TEXT("organic")));
			TestEqual(TEXT("slot0 kind"), Slots.Slots[0].Kind, ESWGDraftSlotKind::Resource);
			TestTrue(TEXT("slot1 optional"), Slots.Slots[1].bOptional);
			TestEqual(TEXT("slot1 kind"), Slots.Slots[1].Kind, ESWGDraftSlotKind::Identical);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGCraftingResourceWeightsParseTest,
	"SWGEmu.Messages.Crafting.ResourceWeights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGCraftingResourceWeightsParseTest::RunTest(const FString& Parameters)
{
	FSWGPacket Pkt;
	Pkt.WriteUInt32(0x9000); Pkt.WriteUInt32(0x9000);
	Pkt.WriteByte(2); // batch count
	WriteResourceWeight(Pkt, { TPair<uint8, uint8>(1, 1) });  // CR, batch weight forced to 1
	WriteResourceWeight(Pkt, { TPair<uint8, uint8>(4, 1) });  // HR
	Pkt.WriteByte(2); // real count
	WriteResourceWeight(Pkt, { TPair<uint8, uint8>(1, 9) });  // CR weight 9
	WriteResourceWeight(Pkt, { TPair<uint8, uint8>(4, 3) });  // HR weight 3
	Pkt.SetReadMode();

	FSWGCraftingResourceWeightsIn Weights;
	if (TestTrue(TEXT("parses"), Weights.Parse(Pkt)))
	{
		TestEqual(TEXT("crc"), Weights.SchematicCrc, 0x9000u);
		if (TestEqual(TEXT("batch count"), Weights.BatchWeights.Num(), 2)
			&& TestEqual(TEXT("real count"), Weights.Weights.Num(), 2))
		{
			TestEqual(TEXT("batch0 property"), Weights.BatchWeights[0].Entries[0].Property, (uint8)1);
			TestEqual(TEXT("batch0 weight forced to 1"), Weights.BatchWeights[0].Entries[0].Weight, (uint8)1);
			TestEqual(TEXT("real0 weight"), Weights.Weights[0].Entries[0].Weight, (uint8)9);
			TestEqual(TEXT("real1 property"), Weights.Weights[1].Entries[0].Property, (uint8)4);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGCraftingIngredientSlotsParseTest,
	"SWGEmu.Messages.Crafting.IngredientSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGCraftingIngredientSlotsParseTest::RunTest(const FString& Parameters)
{
	FSWGPacket Pkt;
	Pkt.WriteUInt64(1); Pkt.WriteUInt64(2); Pkt.WriteUInt64(3);
	Pkt.WriteInt32(2);   // constant
	Pkt.WriteByte(1);    // allowFactory
	Pkt.WriteInt32(1);   // slot count
	WriteDraftSlot(Pkt, TEXT("craft_weapon_components_n"), TEXT("barrel"), false, TEXT(""), ESWGDraftSlotKind::Mixed, 1);
	Pkt.WriteInt16(0);
	Pkt.SetReadMode();

	FSWGCraftingIngredientSlotsIn Slots;
	if (TestTrue(TEXT("parses"), Slots.Parse(Pkt)))
	{
		TestEqual(TEXT("toolId"), Slots.ToolId, 1ull);
		TestEqual(TEXT("mscoId"), Slots.ManufactureSchematicId, 2ull);
		TestEqual(TEXT("prototypeId"), Slots.PrototypeId, 3ull);
		TestEqual(TEXT("allowFactory"), Slots.AllowFactory, (uint8)1);
		if (TestEqual(TEXT("slot count"), Slots.Slots.Num(), 1))
		{
			TestEqual(TEXT("slot0 kind"), Slots.Slots[0].Kind, ESWGDraftSlotKind::Mixed);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGCraftingStatusParseTest,
	"SWGEmu.Messages.Crafting.Status",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGCraftingStatusParseTest::RunTest(const FString& Parameters)
{
	FSWGPacket Pkt;
	Pkt.WriteInt32(0x107);
	Pkt.WriteInt32(static_cast<int32>(ESWGCraftingSlotResult::Full));
	Pkt.WriteByte(5);
	Pkt.SetReadMode();

	FSWGCraftingStatusIn Status;
	if (TestTrue(TEXT("parses"), Status.Parse(Pkt)))
	{
		TestEqual(TEXT("subType"), Status.SubType, 0x107);
		TestEqual(TEXT("value"), Status.Value, static_cast<int32>(ESWGCraftingSlotResult::Full));
		TestEqual(TEXT("counter"), Status.Counter, (uint8)5);
	}

	FSWGPacket ClosePkt;
	ClosePkt.WriteByte(7);
	ClosePkt.SetReadMode();
	FSWGCraftingCloseWindowIn Close;
	if (TestTrue(TEXT("close parses"), Close.Parse(ClosePkt)))
	{
		TestEqual(TEXT("close counter"), Close.Counter, (uint8)7);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGCraftingOutgoingSerializeTest,
	"SWGEmu.Messages.Crafting.OutgoingSerialize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGCraftingOutgoingSerializeTest::RunTest(const FString& Parameters)
{
	// Experiment: two rows.
	{
		TArray<FSWGCraftingExperimentRow> Rows;
		Rows.Add({ 0, 4 });
		Rows.Add({ 2, 6 });
		FSWGCraftingExperimentMessage Msg(999, 3, Rows);
		FSWGPacket Pkt = Msg.Serialize();
		Pkt.SetReadMode();
		TestEqual(TEXT("object controller operand count"), Pkt.ReadUInt16(), static_cast<uint16>(0x05));
		TestEqual(TEXT("exp opcode"), Pkt.ReadUInt32(), 0x80CE5E46u);
		TestEqual(TEXT("exp priority"), Pkt.ReadUInt32(), 0x0Bu);
		TestEqual(TEXT("exp type"), Pkt.ReadUInt32(), 0x106u);
		TestEqual(TEXT("exp objectId"), Pkt.ReadUInt64(), 999ull);
		Pkt.ReadUInt32(); // size, unused
		TestEqual(TEXT("exp counter"), Pkt.ReadByte(), (uint8)3);
		TestEqual(TEXT("exp row count"), Pkt.ReadInt32(), 2);
		TestEqual(TEXT("exp row0"), Pkt.ReadInt32(), 0);
		TestEqual(TEXT("exp row0 points"), Pkt.ReadInt32(), 4);
	}

	// Add ingredient.
	{
		FSWGCraftingAddIngredientMessage Msg(999, 5555, 2, 9);
		FSWGPacket Pkt = Msg.Serialize();
		Pkt.SetReadMode();
		Pkt.ReadUInt16(); Pkt.ReadUInt32();
		TestEqual(TEXT("add priority"), Pkt.ReadUInt32(), 0x0Bu);
		TestEqual(TEXT("add type"), Pkt.ReadUInt32(), 0x107u);
		Pkt.ReadUInt64(); // envelope objectId (=playerId)
		Pkt.ReadUInt32(); // size, unused
		TestEqual(TEXT("add ingredientId"), Pkt.ReadUInt64(), 5555ull);
		TestEqual(TEXT("add slot"), Pkt.ReadInt32(), 2);
		Pkt.ReadUInt32(); // unused
		TestEqual(TEXT("add counter"), Pkt.ReadByte(), (uint8)9);
	}

	// Remove ingredient — field order is slot THEN objectId, reversed from Add.
	{
		FSWGCraftingRemoveIngredientMessage Msg(999, 2, 5555, 9);
		FSWGPacket Pkt = Msg.Serialize();
		Pkt.SetReadMode();
		Pkt.ReadUInt16(); Pkt.ReadUInt32(); Pkt.ReadUInt32();
		TestEqual(TEXT("remove type"), Pkt.ReadUInt32(), 0x108u);
		Pkt.ReadUInt64(); // envelope objectId
		Pkt.ReadUInt32(); // size, unused
		TestEqual(TEXT("remove slot"), Pkt.ReadInt32(), 2);
		TestEqual(TEXT("remove ingredientId"), Pkt.ReadUInt64(), 5555ull);
		TestEqual(TEXT("remove counter"), Pkt.ReadByte(), (uint8)9);
	}

	// Customize.
	{
		TArray<FSWGCraftingCustomizationEdit> Edits;
		Edits.Add({ 0, 12 });
		FSWGCraftingCustomizeMessage Msg(999, TEXT("My Item"), 0xFF, 5, Edits);
		FSWGPacket Pkt = Msg.Serialize();
		Pkt.SetReadMode();
		Pkt.ReadUInt16(); Pkt.ReadUInt32(); Pkt.ReadUInt32();
		TestEqual(TEXT("customize type"), Pkt.ReadUInt32(), 0x15Au);
		Pkt.ReadUInt64(); // envelope objectId
		Pkt.ReadUInt32(); // size, unused
		TestEqual(TEXT("customize name"), Pkt.ReadUnicodeString(), FString(TEXT("My Item")));
		TestEqual(TEXT("customize template"), Pkt.ReadByte(), (uint8)0xFF);
		TestEqual(TEXT("customize count"), Pkt.ReadInt32(), 5);
		TestEqual(TEXT("customize editCount"), Pkt.ReadByte(), (uint8)1);
		TestEqual(TEXT("customize edit0 index"), Pkt.ReadInt32(), 0);
		TestEqual(TEXT("customize edit0 value"), Pkt.ReadInt32(), 12);
	}

	return true;
}

#endif
