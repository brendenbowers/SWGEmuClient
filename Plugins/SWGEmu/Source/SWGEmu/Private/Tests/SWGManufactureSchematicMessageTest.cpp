#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/ManufactureSchematic/ManufactureSchematicBaseline.h"
#include "Network/Objects/Zone/ManufactureSchematic/ManufactureSchematicDelta.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGManufactureSchematicBase3Test,
	"SWGEmu.Messages.ManufactureSchematic.Base3",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGManufactureSchematicBase3Test::RunTest(const FString& Parameters)
{
	FSWGPacket Pkt;
	Pkt.WriteFloat(3.5f);
	Pkt.WriteAsciiString(TEXT("String_id_table"));
	Pkt.WriteInt32(0);
	Pkt.WriteAsciiString(TEXT("dried_fruit"));
	Pkt.WriteUnicodeString(TEXT("Grandma's Dried Fruit"));
	Pkt.WriteInt32(0);   // dataSize, written via insertInt
	Pkt.WriteInt32(50);  // manufactureLimit
	Pkt.WriteInt32(1);
	Pkt.WriteInt32(1);
	Pkt.WriteByte(0);
	Pkt.WriteAsciiString(TEXT("crafting"));
	Pkt.WriteInt32(0);
	Pkt.WriteAsciiString(TEXT("complexity"));
	Pkt.WriteFloat(3.5f);
	Pkt.WriteUnicodeString(TEXT("Han"));
	Pkt.WriteInt32(25);
	Pkt.WriteFloat(8.0f);
	Pkt.SetReadMode();

	FManufactureSchematicBaseline Baseline;
	SWGManufactureSchematicBaselineParser::ParseBase3(Pkt, Baseline);
	TestTrue(TEXT("has base3"), Baseline.bHasBase3);
	TestEqual(TEXT("complexity"), Baseline.Complexity, 3.5f);
	TestEqual(TEXT("object name"), Baseline.ObjectNameName, FString(TEXT("dried_fruit")));
	TestEqual(TEXT("custom name"), Baseline.CustomName, FString(TEXT("Grandma's Dried Fruit")));
	TestEqual(TEXT("manufacture limit"), Baseline.ManufactureLimit, 50);
	TestEqual(TEXT("player name"), Baseline.PlayerName, FString(TEXT("Han")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGManufactureSchematicBase6Test,
	"SWGEmu.Messages.ManufactureSchematic.Base6",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGManufactureSchematicBase6Test::RunTest(const FString& Parameters)
{
	FSWGPacket Pkt;
	Pkt.WriteInt32(0x76);
	Pkt.WriteInt32(0);
	Pkt.WriteUInt32(0xCAFEBABE);
	Pkt.WriteInt16(1);
	Pkt.SetReadMode();

	FManufactureSchematicBaseline Baseline;
	SWGManufactureSchematicBaselineParser::ParseBase6(Pkt, Baseline);
	TestTrue(TEXT("has base6"), Baseline.bHasBase6);
	TestEqual(TEXT("crc"), Baseline.SchematicCrc, 0xCAFEBABEu);
	TestTrue(TEXT("active"), Baseline.bActiveCraft);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGManufactureSchematicBase7Test,
	"SWGEmu.Messages.ManufactureSchematic.Base7",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGManufactureSchematicBase7Test::RunTest(const FString& Parameters)
{
	FSWGPacket Pkt;

	// ingredientNames: 1 StringId.
	Pkt.WriteInt32(1); Pkt.WriteUInt32(1);
	Pkt.WriteAsciiString(TEXT("craft_food_ingredients_n")); Pkt.WriteInt32(0); Pkt.WriteAsciiString(TEXT("dried_fruit"));

	// ingredientTypes: 1 int.
	Pkt.WriteInt32(1); Pkt.WriteUInt32(1); Pkt.WriteInt32(0);

	// slotOIDs: 1 slot, containing 1 uint64.
	Pkt.WriteInt32(1); Pkt.WriteUInt32(1);
	Pkt.WriteInt32(1); Pkt.WriteUInt64(777777ull);

	// slotQuantities: 1 slot, containing 1 int.
	Pkt.WriteInt32(1); Pkt.WriteUInt32(1);
	Pkt.WriteInt32(1); Pkt.WriteInt32(3);

	// slotQualities: 1 float.
	Pkt.WriteInt32(1); Pkt.WriteUInt32(1); Pkt.WriteFloat(0.95f);

	// slotClean: 1 int.
	Pkt.WriteInt32(1); Pkt.WriteUInt32(1); Pkt.WriteInt32(-1);

	// slotIndexes: 1 int.
	Pkt.WriteInt32(1); Pkt.WriteUInt32(1); Pkt.WriteInt32(0);

	Pkt.WriteByte(4); // ingredientCounter

	// Experimenting group titles: 1 group.
	Pkt.WriteInt32(1); Pkt.WriteInt32(1);
	Pkt.WriteAsciiString(TEXT("crafting")); Pkt.WriteInt32(0); Pkt.WriteAsciiString(TEXT("exp_quality"));

	// Experimenting current percentages.
	Pkt.WriteInt32(1); Pkt.WriteInt32(1); Pkt.WriteFloat(0.5f);

	// Useless offsets.
	Pkt.WriteInt32(1); Pkt.WriteInt32(1); Pkt.WriteInt32(0);

	// Max values.
	Pkt.WriteInt32(1); Pkt.WriteInt32(1); Pkt.WriteFloat(1.0f);

	// Five empty palette-related blocks.
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Pkt.WriteInt32(0); Pkt.WriteInt32(0);
	}

	Pkt.WriteByte(0);    // customization counter
	Pkt.WriteFloat(0.1f); // risk factor
	Pkt.WriteInt32(0); Pkt.WriteInt32(0); // template list, empty
	Pkt.WriteByte(1); // ready

	Pkt.SetReadMode();

	FManufactureSchematicBaseline Baseline;
	SWGManufactureSchematicBaselineParser::ParseBase7(Pkt, Baseline);
	TestTrue(TEXT("has base7"), Baseline.bHasBase7);
	if (TestEqual(TEXT("ingredient names"), Baseline.IngredientNames.Items.Num(), 1))
	{
		TestEqual(TEXT("name file"), Baseline.IngredientNames.Items[0].File, FString(TEXT("craft_food_ingredients_n")));
	}
	if (TestEqual(TEXT("slot oids"), Baseline.SlotOIDs.Items.Num(), 1) && TestEqual(TEXT("slot0 oid count"), Baseline.SlotOIDs.Items[0].Num(), 1))
	{
		TestEqual(TEXT("slot0 oid"), Baseline.SlotOIDs.Items[0][0], 777777ull);
	}
	if (TestEqual(TEXT("slot quantities"), Baseline.SlotQuantities.Items.Num(), 1) && TestEqual(TEXT("slot0 qty count"), Baseline.SlotQuantities.Items[0].Num(), 1))
	{
		TestEqual(TEXT("slot0 qty"), Baseline.SlotQuantities.Items[0][0], 3);
	}
	TestEqual(TEXT("ingredient counter"), Baseline.IngredientCounter, (uint8)4);
	if (TestEqual(TEXT("group titles"), Baseline.ExperimentGroupTitles.Num(), 1))
	{
		TestEqual(TEXT("group0 title"), Baseline.ExperimentGroupTitles[0], FString(TEXT("exp_quality")));
	}
	if (TestEqual(TEXT("group percents"), Baseline.ExperimentCurrentPercent.Num(), 1))
	{
		TestEqual(TEXT("group0 percent"), Baseline.ExperimentCurrentPercent[0], 0.5f);
	}
	TestEqual(TEXT("risk factor"), Baseline.RiskFactor, 0.1f);
	TestTrue(TEXT("ready"), Baseline.bReady);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGManufactureSchematicDeltaTest,
	"SWGEmu.Messages.ManufactureSchematic.Delta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGManufactureSchematicDeltaTest::RunTest(const FString& Parameters)
{
	// Delta3: complexity + crafting values list.
	{
		FSWGPacket Pkt;
		Pkt.WriteUInt16(0); Pkt.WriteFloat(4.0f); // field 0
		Pkt.WriteUInt16(5); // field 5: crafting values list
		Pkt.WriteInt32(1); Pkt.WriteInt32(1);
		Pkt.WriteByte(0); Pkt.WriteAsciiString(TEXT("crafting")); Pkt.WriteInt32(0);
		Pkt.WriteAsciiString(TEXT("hp")); Pkt.WriteFloat(0.75f);
		Pkt.SetReadMode();

		FManufactureSchematicDelta Delta;
		SWGManufactureSchematicDeltaParser::ParseDelta3(Pkt, Delta, 2);
		TestTrue(TEXT("has complexity"), Delta.Complexity.IsSet());
		TestEqual(TEXT("complexity"), Delta.Complexity.GetValue(), 4.0f);
		if (TestTrue(TEXT("has crafting values"), Delta.CraftingValues.IsSet())
			&& TestEqual(TEXT("crafting value count"), Delta.CraftingValues->Num(), 1))
		{
			TestEqual(TEXT("attribute name"), (*Delta.CraftingValues)[0].AttributeName, FString(TEXT("hp")));
			TestEqual(TEXT("attribute value"), (*Delta.CraftingValues)[0].Value, 0.75f);
		}
	}

	// Delta6: highlight slot.
	{
		FSWGPacket Pkt;
		Pkt.WriteUInt16(5); Pkt.WriteByte(2);
		Pkt.SetReadMode();
		FManufactureSchematicDelta Delta;
		SWGManufactureSchematicDeltaParser::ParseDelta6(Pkt, Delta, 1);
		TestTrue(TEXT("has highlight"), Delta.HighlightSlot.IsSet());
		TestEqual(TEXT("highlight slot"), Delta.HighlightSlot.GetValue(), (uint8)2);
	}

	// Delta7: group titles (0x08), percentages (0x09), defaults (0x0E), failure rate (0x12), templates (0x13).
	{
		FSWGPacket Pkt;
		Pkt.WriteUInt16(0x08);
		Pkt.WriteInt32(1); Pkt.WriteInt32(1);
		Pkt.WriteByte(1); Pkt.WriteUInt16(0);
		Pkt.WriteAsciiString(TEXT("crafting")); Pkt.WriteInt32(0); Pkt.WriteAsciiString(TEXT("exp_speed"));

		Pkt.WriteUInt16(0x09);
		Pkt.WriteInt32(1); Pkt.WriteInt32(3);
		Pkt.WriteByte(2); Pkt.WriteUInt16(0); Pkt.WriteFloat(0.33f);
		Pkt.WriteUInt16(0x0E);
		Pkt.WriteInt32(1); Pkt.WriteInt32(1);
		Pkt.WriteByte(1); Pkt.WriteUInt16(0); Pkt.WriteInt32(7);

		Pkt.WriteUInt16(0x12);
		Pkt.WriteFloat(0.1f);

		Pkt.WriteUInt16(0x13);
		Pkt.WriteInt32(1); Pkt.WriteInt32(1);
		Pkt.WriteByte(1); Pkt.WriteUInt16(0); Pkt.WriteAsciiString(TEXT("object/tangible/food/shared_food.iff"));

		Pkt.SetReadMode();

		FManufactureSchematicDelta Delta;
		SWGManufactureSchematicDeltaParser::ParseDelta7(Pkt, Delta, 5);
		if (TestTrue(TEXT("has titles"), Delta.GroupTitles.IsSet()) && TestEqual(TEXT("title count"), Delta.GroupTitles->Changes.Num(), 1))
		{
			TestEqual(TEXT("title0"), Delta.GroupTitles->Changes[0].Value, FString(TEXT("exp_speed")));
		}
		if (TestTrue(TEXT("has percents"), Delta.GroupCurrentPercent.IsSet()) && TestEqual(TEXT("percent count"), Delta.GroupCurrentPercent->Changes.Num(), 1))
		{
			TestEqual(TEXT("percent0"), Delta.GroupCurrentPercent->Changes[0].Value, 0.33f);
		}
		if (TestTrue(TEXT("has defaults"), Delta.CustomizationVarDefaults.IsSet()) && TestEqual(TEXT("default count"), Delta.CustomizationVarDefaults->Changes.Num(), 1))
		{
			TestEqual(TEXT("default0"), Delta.CustomizationVarDefaults->Changes[0].Value, 7);
			TArray<int32> Defaults;
			ApplyIndexedListChanges(*Delta.CustomizationVarDefaults, Defaults);
			if (TestEqual(TEXT("applied default count"), Defaults.Num(), 1))
			{
				TestEqual(TEXT("applied default"), Defaults[0], 7);
			}
		}
		TestTrue(TEXT("has failure rate"), Delta.FailureRate.IsSet());
		TestEqual(TEXT("failure rate"), Delta.FailureRate.GetValue(), 0.1f);
		if (TestTrue(TEXT("has templates"), Delta.Templates.IsSet()) && TestEqual(TEXT("template count"), Delta.Templates->Changes.Num(), 1))
		{
			TestEqual(TEXT("template0"), Delta.Templates->Changes[0].Value, FString(TEXT("object/tangible/food/shared_food.iff")));
		}
	}

	return true;
}

#endif
