#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "Region/SWGRegionSet.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGDataTableReader.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGRegionLookupTest, "SWGEmu.Region.Lookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGRegionLookupTest::RunTest(const FString& Parameters)
{
	FSWGDataTableData Table;
	Table.ColumnNames = { TEXT("Name"), TEXT("X"), TEXT("Z"), TEXT("Radius") };
	auto AddRow = [&Table](const TCHAR* Name, const TCHAR* X, const TCHAR* Z, const TCHAR* Radius)
	{
		Table.Rows.AddDefaulted_GetRef().Cells = { Name, X, Z, Radius };
	};
	AddRow(TEXT("@t:big"), TEXT("0"), TEXT("0"), TEXT("1000"));
	AddRow(TEXT("@t:small"), TEXT("100"), TEXT("0"), TEXT("200"));
	AddRow(TEXT("@t:far"), TEXT("5000"), TEXT("-3000"), TEXT("300"));

	FSWGRegionSet Set;
	TestTrue(TEXT("table loads"), Set.LoadFromTable(Table));
	TestEqual(TEXT("three regions"), Set.Regions.Num(), 3);
	TestEqual(TEXT("small centre"), Set.FindContaining(FVector2D(100, 0)), 1);
	TestEqual(TEXT("innermost wins inside the big one"), Set.FindContaining(FVector2D(150, 50)), 1);
	TestEqual(TEXT("big only"), Set.FindContaining(FVector2D(-500, 0)), 0);
	TestEqual(TEXT("edge is outside"), Set.FindContaining(FVector2D(1000, 0)), INDEX_NONE);
	TestEqual(TEXT("negative north"), Set.FindContaining(FVector2D(5000, -3100)), 2);
	TestEqual(TEXT("nothing out in the open"), Set.FindContaining(FVector2D(3000, 3000)), INDEX_NONE);
	TestEqual(TEXT("near the far region"), Set.FindNear(FVector2D(5000, -2600), 150.f), 2);
	TestEqual(TEXT("not near enough"), Set.FindNear(FVector2D(3000, 3000), 150.f), INDEX_NONE);

	FSWGDataTableData Bad;
	Bad.ColumnNames = { TEXT("Name"), TEXT("X") };
	TestFalse(TEXT("missing columns are rejected"), Set.LoadFromTable(Bad));
	TestEqual(TEXT("and leave nothing"), Set.Regions.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGRegionRetailDataTest, "SWGEmu.Region.RetailData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGRegionRetailDataTest::RunTest(const FString& Parameters)
{
	UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
	USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
	if (!Tre)
	{
		AddError(TEXT("could not construct a TRE subsystem"));
		return false;
	}
	Tre->AddToRoot();
	Tre->LoadConfig();
	if (!Tre->LoadArchives())
	{
		AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
		Tre->RemoveFromRoot();
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	const TCHAR* Planets[] = { TEXT("tatooine"), TEXT("corellia"), TEXT("naboo"), TEXT("dantooine"), TEXT("dathomir"), TEXT("endor"), TEXT("lok"), TEXT("rori"), TEXT("talus"), TEXT("yavin4") };
	for (const TCHAR* Planet : Planets)
	{
		FSWGDataTableData Table;
		const FString Path = FString::Printf(TEXT("datatables/clientregion/%s.iff"), Planet);
		if (!TestTrue(*FString::Printf(TEXT("%s reads"), *Path), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(Path), Table)))
		{
			continue;
		}
		FSWGRegionSet Set;
		TestTrue(*FString::Printf(TEXT("%s has the region columns"), *Path), Set.LoadFromTable(Table));
		TestTrue(*FString::Printf(TEXT("%s has regions"), *Path), Set.Regions.Num() > 0);
		for (int32 Index = 0; Index < Set.Regions.Num(); ++Index)
		{
			const FSWGClientRegion& Region = Set.Regions[Index];
			TestTrue(*FString::Printf(TEXT("%s region %d has a radius"), *Path, Index), Region.Radius > 0.f);
			TestTrue(*FString::Printf(TEXT("%s region %d contains its centre"), *Path, Index), Region.Contains(Region.Center) && Set.FindContaining(Region.Center) != INDEX_NONE);
			const FString Name = Tre->ResolveStringId(Region.NameReference);
			TestTrue(*FString::Printf(TEXT("%s: '%s' resolves to text"), *Path, *Region.NameReference), !Name.IsEmpty() && !Name.StartsWith(TEXT("@")));
		}
	}

	// Bestine is a named tatooine region.
	FSWGDataTableData Tatooine;
	FSWGRegionSet TatooineSet;
	if (FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/clientregion/tatooine.iff")), Tatooine) && TatooineSet.LoadFromTable(Tatooine))
	{
		const int32 Bestine = TatooineSet.Regions.IndexOfByPredicate([](const FSWGClientRegion& Region) { return Region.NameReference.EndsWith(TEXT(":bestine")); });
		if (TestTrue(TEXT("tatooine has bestine"), Bestine != INDEX_NONE))
		{
			TestEqual(TEXT("bestine name"), Tre->ResolveStringId(TatooineSet.Regions[Bestine].NameReference), FString(TEXT("Bestine")));
		}
	}
	return true;
}

#endif
