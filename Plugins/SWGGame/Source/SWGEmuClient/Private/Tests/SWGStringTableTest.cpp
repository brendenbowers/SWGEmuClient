#include "Misc/AutomationTest.h"

#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGStringTableReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Byte-level STF fixture builder; same layout FSWGStringTableReader documents.
	struct FStfBuilder
	{
		TArray<uint8> Bytes;

		void U8(uint8 Value) { Bytes.Add(Value); }
		void U32(uint32 Value) { for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex) { Bytes.Add((uint8)(Value >> (8 * ByteIndex))); } }
		void Utf16(const FString& Text) { U32(Text.Len()); for (TCHAR Char : Text) { Bytes.Add((uint8)Char); Bytes.Add((uint8)(Char >> 8)); } }
		void Ascii(const FString& Text) { U32(Text.Len()); for (TCHAR Char : Text) { Bytes.Add((uint8)Char); } }
	};

	USWGTreSubsystem* LoadTreSubsystem(FAutomationTestBase& Test)
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
		USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
		if (!Tre)
		{
			Test.AddError(TEXT("could not construct a TRE subsystem"));
			return nullptr;
		}
		Tre->AddToRoot();
		Tre->LoadConfig();
		if (!Tre->LoadArchives())
		{
			Test.AddError(TEXT("no .tre archives loaded — check TreDirectory in DefaultGame.ini"));
			Tre->RemoveFromRoot();
			return nullptr;
		}
		return Tre;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGStringTableDecodeTest,
	"SWGEmu.Tre.StringTable.Decode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::SmokeFilter)

bool FSWGStringTableDecodeTest::RunTest(const FString& Parameters)
{
	// Two entries, deliberately written with the value and key sections in
	// different orders so the id pairing (not position) is what's tested.
	FStfBuilder Builder;
	Builder.U32(0x0000ABCD); Builder.U8(0); Builder.U32(3); Builder.U32(2);
	Builder.U32(1); Builder.U32(0xFFFFFFFF); Builder.Utf16(TEXT("a bantha"));
	Builder.U32(2); Builder.U32(0xFFFFFFFF); Builder.Utf16(TEXT("Cantina"));
	Builder.U32(2); Builder.Ascii(TEXT("sign_cantina"));
	Builder.U32(1); Builder.Ascii(TEXT("bantha"));

	FSWGStringTable Table;
	TestTrue(TEXT("decodes"), FSWGStringTableReader::Read(Builder.Bytes, Table));
	TestEqual(TEXT("entry count"), Table.Entries.Num(), 2);
	TestEqual(TEXT("bantha"), Table.Find(TEXT("bantha")) ? *Table.Find(TEXT("bantha")) : FString(), FString(TEXT("a bantha")));
	TestEqual(TEXT("sign_cantina"), Table.Find(TEXT("sign_cantina")) ? *Table.Find(TEXT("sign_cantina")) : FString(), FString(TEXT("Cantina")));

	// Bad magic and a truncated file both fail cleanly.
	FSWGStringTable Junk;
	TArray<uint8> BadMagic = Builder.Bytes; BadMagic[0] = 0;
	TestFalse(TEXT("bad magic rejected"), FSWGStringTableReader::Read(BadMagic, Junk));
	TArray<uint8> Truncated(Builder.Bytes.GetData(), Builder.Bytes.Num() - 3);
	TestFalse(TEXT("truncated rejected"), FSWGStringTableReader::Read(Truncated, Junk));

	FString ParsedTable, ParsedKey;
	TestTrue(TEXT("@table:key parses"), FSWGStringTableReader::ParseStringId(TEXT("@mob/creature_names:bantha"), ParsedTable, ParsedKey));
	TestEqual(TEXT("table"), ParsedTable, FString(TEXT("mob/creature_names")));
	TestEqual(TEXT("key"), ParsedKey, FString(TEXT("bantha")));
	TestTrue(TEXT("no @ still parses"), FSWGStringTableReader::ParseStringId(TEXT("cbt_spam:attack_hit"), ParsedTable, ParsedKey));
	TestFalse(TEXT("literal text is not a reference"), FSWGStringTableReader::ParseStringId(TEXT("Hello there"), ParsedTable, ParsedKey));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGStringTableRealDataTest,
	"SWGEmu.Tre.StringTable.RealData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGStringTableRealDataTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreSubsystem(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// shared_bantha.iff: objectName = monster_name:bantha (confirmed by hex dump).
	FString Table, Text;
	TestTrue(TEXT("bantha template objectName"), Tre->FindTemplateStringId(TEXT("object/mobile/shared_bantha.iff"), TEXT("objectName"), Table, Text));
	TestEqual(TEXT("table"), Table, FString(TEXT("monster_name")));
	TestEqual(TEXT("key"), Text, FString(TEXT("bantha")));

	const FString Name = Tre->LookupString(Table, Text);
	TestFalse(TEXT("monster_name:bantha resolves"), Name.IsEmpty());
	AddInfo(FString::Printf(TEXT("monster_name:bantha -> \"%s\""), *Name));

	TestEqual(TEXT("literal passthrough"), Tre->ResolveStringId(TEXT("Hello")), FString(TEXT("Hello")));
	TestEqual(TEXT("missing key falls back to key"), Tre->ResolveStringId(TEXT("@monster_name:no_such_creature_xyz")), FString(TEXT("no_such_creature_xyz")));
	TestNull(TEXT("missing table is null"), Tre->GetStringTable(TEXT("no/such_table")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
