#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGAsteroidFieldReader.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	FSWGDataTableData MakeStyleTable(const FString& ScaleMin, const FString& ScaleMax, const FString& Spawn, const FString& Delete)
	{
		FSWGDataTableData Table;
		Table.ColumnNames = {TEXT("Appearance"), TEXT("Likelihood"), TEXT("ScaleMin"), TEXT("ScaleMax"), TEXT("SpawnRange"), TEXT("DeleteRange"), TEXT("HitEffect")};
		Table.ColumnTypes = {TEXT('s'), TEXT('f'), TEXT('f'), TEXT('f'), TEXT('f'), TEXT('f'), TEXT('s')};
		Table.Rows.AddDefaulted();
		Table.Rows[0].Cells = {TEXT("appearance/asteroid_radial_s01.apt"), TEXT("1"), ScaleMin, ScaleMax, Spawn, Delete, TEXT("clienteffect/x.cef")};
		return Table;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceAsteroidFieldTest, "SWG.Space.AsteroidFieldReader", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpaceAsteroidFieldTest::RunTest(const FString& Parameters)
{
	// Synthetic style tables: the happy path and each rejection, with the output left untouched on failure.
	TArray<FSWGAsteroidRadialStyleRow> Style;
	TestTrue(TEXT("a well-formed style row parses"), FSWGAsteroidFieldReader::ReadRadialStyle(MakeStyleTable(TEXT("0.5"), TEXT("1"), TEXT("100"), TEXT("150")), Style));
	if (TestEqual(TEXT("one style row"), Style.Num(), 1))
	{
		TestEqual(TEXT("style scale min"), Style[0].ScaleMin, .5f);
		TestEqual(TEXT("style delete range"), Style[0].DeleteRange, 150.f);
		TestEqual(TEXT("style hit effect"), Style[0].HitEffect, FString(TEXT("clienteffect/x.cef")));
	}
	struct FBad { const TCHAR* Label; FString ScaleMin, ScaleMax, Spawn, Delete; };
	const FBad Bad[] = {
		{TEXT("nan scale"), TEXT("nan"), TEXT("1"), TEXT("100"), TEXT("100")},
		{TEXT("zero scale"), TEXT("0"), TEXT("1"), TEXT("100"), TEXT("100")},
		{TEXT("inverted scale"), TEXT("2"), TEXT("1"), TEXT("100"), TEXT("100")},
		{TEXT("huge scale"), TEXT("1"), TEXT("1000"), TEXT("100"), TEXT("100")},
		{TEXT("text scale"), TEXT("1"), TEXT("big"), TEXT("100"), TEXT("100")},
		{TEXT("zero spawn range"), TEXT("1"), TEXT("1"), TEXT("0"), TEXT("100")},
		{TEXT("delete inside spawn"), TEXT("1"), TEXT("1"), TEXT("100"), TEXT("50")},
		{TEXT("huge delete range"), TEXT("1"), TEXT("1"), TEXT("100"), TEXT("1e9")},
	};
	for (const FBad& Case : Bad)
		TestFalse(*FString::Printf(TEXT("style with %s is rejected"), Case.Label), FSWGAsteroidFieldReader::ReadRadialStyle(MakeStyleTable(Case.ScaleMin, Case.ScaleMax, Case.Spawn, Case.Delete), Style));
	TestEqual(TEXT("a rejected table leaves the earlier result"), Style.Num(), 1);
	FSWGDataTableData Missing = MakeStyleTable(TEXT("1"), TEXT("1"), TEXT("100"), TEXT("100"));
	Missing.ColumnNames[Missing.GetColumnIndex(TEXT("SpawnRange"))] = TEXT("Other");
	TestFalse(TEXT("a style table without SpawnRange is rejected"), FSWGAsteroidFieldReader::ReadRadialStyle(Missing, Style));
	FSWGDataTableData Empty = MakeStyleTable(TEXT("1"), TEXT("1"), TEXT("100"), TEXT("100"));
	Empty.Rows.Reset();
	TestFalse(TEXT("an empty style table is rejected"), FSWGAsteroidFieldReader::ReadRadialStyle(Empty, Style));

	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->AddToRoot(); Game->InitializeStandalone();
	UWorld* World = Game->GetWorldContext()->World();
	if (!World) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
	ON_SCOPE_EXIT
	{
		Game->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
		World->RemoveFromRoot(); Game->RemoveFromRoot();
	};
	World->InitializeActorsForPlay(FURL());
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	if (!Tre) return false;
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives available"), Tre->LoadArchives())) return false;

	// Retail radial_s01: three rocks, scale 0.5..1, spawn and delete range 100.
	FSWGDataTableData StyleTable;
	if (!TestTrue(TEXT("radial_s01 decodes"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/space/asteroidfield/fieldstyle/radial_s01.iff")), StyleTable))) return false;
	TArray<FSWGAsteroidRadialStyleRow> Radial;
	if (TestTrue(TEXT("radial_s01 parses"), FSWGAsteroidFieldReader::ReadRadialStyle(StyleTable, Radial)) && TestEqual(TEXT("radial_s01 has three rocks"), Radial.Num(), 3))
	{
		TestEqual(TEXT("first rock appearance"), Radial[0].Appearance, FString(TEXT("appearance/asteroid_radial_s01.apt")));
		for (const FSWGAsteroidRadialStyleRow& Row : Radial)
		{
			TestEqual(TEXT("rock scale min"), Row.ScaleMin, .5f);
			TestEqual(TEXT("rock scale max"), Row.ScaleMax, 1.f);
			TestEqual(TEXT("rock spawn range"), Row.SpawnRange, 100.f);
			TestEqual(TEXT("rock delete range"), Row.DeleteRange, 100.f);
		}
	}
	// Ship debris tables are 1..1 and weighted (death star chunks are 5x the fighters).
	FSWGDataTableData DebrisTable;
	TArray<FSWGAsteroidRadialStyleRow> Debris;
	if (TestTrue(TEXT("radial_debris_death_star decodes"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/space/asteroidfield/fieldstyle/radial_debris_death_star.iff")), DebrisTable))
		&& TestTrue(TEXT("radial_debris_death_star parses"), FSWGAsteroidFieldReader::ReadRadialStyle(DebrisTable, Debris)))
	{
		TestEqual(TEXT("death star debris rows"), Debris.Num(), 21);
		TestEqual(TEXT("wreckage likelihood"), Debris.Last().Likelihood, 5.f);
		TestEqual(TEXT("debris scale"), Debris[0].ScaleMax, 1.f);
	}

	// The zone field table: radial counts and ranges per row.
	FSWGDataTableData FieldTable;
	TArray<FSWGAsteroidFieldData> Fields;
	if (!TestTrue(TEXT("Tatooine field table decodes"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/space/asteroidfield/space_tatooine.iff")), FieldTable))
		|| !TestTrue(TEXT("Tatooine field table parses"), FSWGAsteroidFieldReader::Read(FieldTable, Fields)) || !TestEqual(TEXT("Tatooine field rows"), Fields.Num(), 25)) return false;
	TestEqual(TEXT("first field radial count"), Fields[0].NumRadialAsteroids, 50);
	TestEqual(TEXT("first field view all distance"), Fields[0].ViewAllDistance, 700.f);
	TestEqual(TEXT("first field max viewable distance"), Fields[0].MaxViewableDistance, 2000.f);
	TestEqual(TEXT("first field scale min"), Fields[0].ScaleMin, 1.f);
	TestEqual(TEXT("first field scale max"), Fields[0].ScaleMax, 1.f);
	TestEqual(TEXT("first field style table"), Fields[0].RadialStyleTable, FString(TEXT("datatables/space/asteroidfield/fieldstyle/radial_s01.iff")));
	TestEqual(TEXT("an x-wing field names the debris table"), Fields[4].RadialStyleTable, FString(TEXT("datatables/space/asteroidfield/fieldstyle/radial_debris_xwingvstie.iff")));
	TestEqual(TEXT("an x-wing field radial count"), Fields[4].NumRadialAsteroids, 74);

	// Field-row rejections: an out-of-range radial count and a bad scale range leave the earlier output alone.
	const int32 CountColumn = FieldTable.GetColumnIndex(TEXT("NumRadialAsteroids"));
	const FString Count = FieldTable.Rows[0].Cells[CountColumn];
	for (const TCHAR* Invalid : {TEXT("-1"), TEXT("100000"), TEXT("nan"), TEXT("many")})
	{
		FieldTable.Rows[0].Cells[CountColumn] = Invalid;
		TestFalse(*FString::Printf(TEXT("radial count '%s' is rejected"), Invalid), FSWGAsteroidFieldReader::Read(FieldTable, Fields));
		TestEqual(TEXT("failure preserves the authored fields"), Fields.Num(), 25);
	}
	FieldTable.Rows[0].Cells[CountColumn] = Count;
	const int32 ScaleMaxColumn = FieldTable.GetColumnIndex(TEXT("ScaleMax"));
	const FString ScaleMax = FieldTable.Rows[0].Cells[ScaleMaxColumn];
	for (const TCHAR* Invalid : {TEXT("0.5"), TEXT("nan"), TEXT("500"), TEXT("tall")})
	{
		FieldTable.Rows[0].Cells[ScaleMaxColumn] = Invalid;
		TestFalse(*FString::Printf(TEXT("field scale max '%s' is rejected"), Invalid), FSWGAsteroidFieldReader::Read(FieldTable, Fields));
	}
	FieldTable.Rows[0].Cells[ScaleMaxColumn] = ScaleMax;
	TestTrue(TEXT("the restored table parses again"), FSWGAsteroidFieldReader::Read(FieldTable, Fields));
	return true;
}
#endif
