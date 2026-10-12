#include "Misc/AutomationTest.h"

#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGCrc32.h"
#include "TRE/SWGPobReader.h"
#include "TRE/SWGFloorReader.h"
#include "TRE/SWGFootprintReader.h"
#include "TRE/SWGMeshReader.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGGreatHouseAssetTest, "SWGEmu.Tre.Housing.GreatHouse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGGreatHouseAssetTest::RunTest(const FString& Parameters)
{
	UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
	USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
	if (!Tre) return false;
	Tre->AddToRoot();
	ON_SCOPE_EXIT { Tre->RemoveFromRoot(); };
	Tre->LoadConfig();
	if (!TestTrue(TEXT("TRE archives load"), Tre->LoadArchives())) return false;

	const FString Building = TEXT("object/building/player/shared_player_house_tatooine_great_style_01.iff");
	TestEqual(TEXT("custom CRC resolves"), Tre->ResolveTemplatePath(FSWGCrc32::HashString(Building)), Building);
	TestTrue(TEXT("custom building template exists"), Tre->CreateIffReader(Building).IsValid());

	FSWGPobData Pob;
	if (!TestTrue(TEXT("two-cell portal layout parses"), FSWGPobReader::ReadPob(
		Tre->CreateIffReader(TEXT("appearance/ply_tato_house_gh_s01_fp1.pob")), Pob))) return false;
	if (!TestEqual(TEXT("exterior plus one room"), Pob.Cells.Num(), 2)) return false;
	for (const FSWGPobCell& Cell : Pob.Cells)
	{
		TestEqual(TEXT("both doors connect this cell"), Cell.Portals.Num(), 2);
		for (const FSWGPobPortalRef& Portal : Cell.Portals)
		{
			TestEqual(TEXT("portal target"), Portal.ConnectingCellIndex, Cell.CellIndex == 0 ? 1 : 0);
			TestEqual(TEXT("portal opening corners"), Portal.OpeningVertices.Num(), 4);
		}
		FSWGMeshData Mesh;
		TestTrue(TEXT("room mesh parses"), FSWGMeshReader::ReadStaticMesh(Tre->CreateIffReader(Cell.MeshPath), Mesh));
		TestTrue(TEXT("room mesh has geometry"), !Mesh.Submeshes.IsEmpty());
	}

	FSWGFloorData Floor;
	TestTrue(TEXT("hall floor parses"), FSWGFloorReader::ReadFloor(Tre->CreateIffReader(Pob.Cells[1].CollisionFloorPath), Floor));
	TestEqual(TEXT("hall has two floor triangles"), Floor.Triangles.Num(), 2);
	FSWGStructureFootprint Footprint;
	TestTrue(TEXT("footprint parses"), FSWGFootprintReader::Read(Tre->CreateIffReader(
		TEXT("footprint/building/player/shared_player_house_tatooine_great_style_01.sfp")), Footprint));
	TestEqual(TEXT("footprint columns"), Footprint.Cols, 4);
	TestEqual(TEXT("footprint rows"), Footprint.Rows, 4);

	const FString Tower = TEXT("object/building/player/shared_player_house_tatooine_tower_style_01.iff");
	TestEqual(TEXT("tower CRC resolves"), Tre->ResolveTemplatePath(FSWGCrc32::HashString(Tower)), Tower);
	TestTrue(TEXT("tower building template exists"), Tre->CreateIffReader(Tower).IsValid());
	FSWGPobData TowerPob;
	if (!TestTrue(TEXT("tower portal layout parses"), FSWGPobReader::ReadPob(
		Tre->CreateIffReader(TEXT("appearance/ply_tato_house_tw_s01_fp1.pob")), TowerPob))) return false;
	if (!TestEqual(TEXT("tower exterior and four-story room"), TowerPob.Cells.Num(), 2)) return false;
	TestEqual(TEXT("tower has two entrances"), TowerPob.Cells[1].Portals.Num(), 2);
	for (const FSWGPobCell& TowerCell : TowerPob.Cells)
	{
		FSWGMeshData TowerMesh;
		TestTrue(TEXT("tower mesh parses"), FSWGMeshReader::ReadStaticMesh(Tre->CreateIffReader(TowerCell.MeshPath), TowerMesh));
		TestTrue(TEXT("tower mesh has geometry"), !TowerMesh.Submeshes.IsEmpty());
	}
	FSWGFloorData TowerFloor;
	TestTrue(TEXT("tower floor and ramps parse"), FSWGFloorReader::ReadFloor(
		Tre->CreateIffReader(TowerPob.Cells[1].CollisionFloorPath), TowerFloor));
	TestEqual(TEXT("tower decks and ramps"), TowerFloor.Triangles.Num(), 88);
	FSWGStructureFootprint TowerFootprint;
	TestTrue(TEXT("tower footprint parses"), FSWGFootprintReader::Read(Tre->CreateIffReader(
		TEXT("footprint/building/player/shared_player_house_tatooine_tower_style_01.sfp")), TowerFootprint));
	TestEqual(TEXT("tower footprint columns"), TowerFootprint.Cols, 4);
	TestEqual(TEXT("tower footprint rows"), TowerFootprint.Rows, 4);

	for (const TPair<FString, FString>& Deed : {
		TPair<FString, FString>(TEXT("great"), TEXT("Tatooine Great Hall Deed")),
		TPair<FString, FString>(TEXT("tower"), TEXT("Tatooine Roof Tower Deed"))
	})
	{
		const FString Path = FString::Printf(TEXT("object/tangible/deed/player_house_deed/shared_tatooine_house_%s_style_01_deed.iff"), *Deed.Key);
		TestEqual(TEXT("custom deed CRC resolves"), Tre->ResolveTemplatePath(FSWGCrc32::HashString(Path)), Path);
		TestTrue(TEXT("custom deed template exists"), Tre->CreateIffReader(Path).IsValid());
		TestEqual(TEXT("custom deed name"), Tre->ResolveTemplateObjectName(FSWGCrc32::HashString(Path)), Deed.Value);
		FString Table, Key;
		if (!TestTrue(TEXT("custom deed description field"), Tre->FindTemplateStringId(Path, TEXT("detailedDescription"), Table, Key))) return false;
		TestTrue(TEXT("custom deed description text"), Tre->LookupString(Table, Key).Contains(TEXT("one-lot")));
	}
	return true;
}

#endif
