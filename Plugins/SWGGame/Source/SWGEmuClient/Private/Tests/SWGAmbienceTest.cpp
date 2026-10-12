#include "Misc/AutomationTest.h"

#include "Audio/SWGInteriorTable.h"
#include "Audio/SWGReverbPresets.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "Objects/World/SWGSoundEmitter.h"
#include "SWGAudioTypes.h"
#include "Sound/ReverbEffect.h"
#include "Subsystems/SWGAmbienceSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGPobReader.h"
#include "TRE/SWGTerrainEvaluator.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGWorldSnapshotReader.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Reads the real game files: EGRP/AENV decode, environment and interior tables, and sound object templates. */
namespace
{
	USWGTreSubsystem* LoadTreForAmbienceTest(FAutomationTestBase& Test)
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
			Test.AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
			Tre->RemoveFromRoot();
			return nullptr;
		}
		return Tre;
	}

	bool SoundExists(const USWGTreSubsystem& Tre, const FString& Reference)
	{
		return Tre.FileExists(SWGAudio::NormalizeSoundPath(Reference));
	}

	/** Planets that have an environment table, by base name ("tatooine", "space_naboo"). */
	TArray<FString> FindEnvironmentTablePlanets(const USWGTreSubsystem& Tre)
	{
		TArray<FString> Planets;
		for (const FString& Path : Tre.FindVirtualPaths(TEXT("datatables/environment/")))
		{
			if (Path.EndsWith(TEXT(".iff")))
			{
				Planets.AddUnique(FPaths::GetBaseFilename(Path));
			}
		}
		Planets.Sort();
		return Planets;
	}

	void CollectEnvironmentAffectors(const TArray<FSWGTerrainLayer>& Layers, TArray<const FSWGTerrainAffector*>& OutAffectors)
	{
		for (const FSWGTerrainLayer& Layer : Layers)
		{
			for (const FSWGTerrainAffector& Affector : Layer.Affectors)
			{
				if (Affector.Type == ESWGTerrainAffectorType::Environment)
				{
					OutAffectors.Add(&Affector);
				}
			}
			CollectEnvironmentAffectors(Layer.Children, OutAffectors);
		}
	}

	void CountSoundObjects(const FSWGWorldSnapshotData& Snapshot, const TArray<FSWGWorldSnapshotNode>& Nodes, int32& OutCount, TSet<FString>& OutTemplates)
	{
		for (const FSWGWorldSnapshotNode& Node : Nodes)
		{
			if (Snapshot.ObjectTemplateNames.IsValidIndex(static_cast<int32>(Node.NameID)) && ASWGSoundEmitter::IsSoundObjectTemplate(Snapshot.ObjectTemplateNames[Node.NameID]))
			{
				++OutCount;
				OutTemplates.Add(Snapshot.ObjectTemplateNames[Node.NameID]);
			}
			CountSoundObjects(Snapshot, Node.Children, OutCount, OutTemplates);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGTerrainEnvironmentTest,
	"SWGEmu.Tre.Terrain.Environment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGTerrainEnvironmentTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForAmbienceTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	int32 PlanetsParsed = 0;
	int32 PlanetsWithEnvironment = 0;
	for (const FString& Planet : FindEnvironmentTablePlanets(*Tre))
	{
		const FString TerrainPath = FString::Printf(TEXT("terrain/%s.trn"), *Planet);
		// space_* terrains are not PTAT planets.
		if (Planet.StartsWith(TEXT("space_")) || !Tre->FileExists(TerrainPath))
		{
			continue;
		}

		FSWGTerrainData Data;
		if (!TestTrue(*FString::Printf(TEXT("%s parses"), *TerrainPath), FSWGTerrainReader::ReadTerrain(Tre->CreateIffReader(TerrainPath), Data)))
		{
			continue;
		}
		++PlanetsParsed;

		TArray<const FSWGTerrainAffector*> Affectors;
		CollectEnvironmentAffectors(Data.TopLevelLayers, Affectors);
		TSet<int32> UsedIds;
		for (const FSWGTerrainAffector* Affector : Affectors)
		{
			UsedIds.Add(Affector->EnvironmentFamilyId);
			TestNotNull(*FString::Printf(TEXT("%s AENV family %d is in EGRP"), *Planet, Affector->EnvironmentFamilyId), Data.FindEnvironmentFamily(Affector->EnvironmentFamilyId));
			TestTrue(*FString::Printf(TEXT("%s AENV unknown int is a 0/1 flag"), *Planet), Affector->EnvironmentUnknown == 0 || Affector->EnvironmentUnknown == 1);
			TestTrue(*FString::Printf(TEXT("%s AENV weight is in 0..1"), *Planet), Affector->EnvironmentWeight >= 0.0f && Affector->EnvironmentWeight <= 1.0f);
			if (Affector->EnvironmentWeight != 1.0f)
			{
				AddInfo(FString::Printf(TEXT("%s AENV family %d weight %.3f"), *Planet, Affector->EnvironmentFamilyId, Affector->EnvironmentWeight));
			}
		}
		for (const FSWGEnvironmentFamily& Family : Data.EnvironmentFamilies)
		{
			TestTrue(*FString::Printf(TEXT("%s EFAM %s weight is in 0..1"), *Planet, *Family.Name), Family.Weight >= 0.0f && Family.Weight <= 1.0f);
			if (Family.Weight != 1.0f)
			{
				AddInfo(FString::Printf(TEXT("%s EFAM %s (id %d) weight %.3f"), *Planet, *Family.Name, Family.FamilyId, Family.Weight));
			}
		}

		if (!Data.EnvironmentFamilies.IsEmpty())
		{
			++PlanetsWithEnvironment;
		}
		AddInfo(FString::Printf(TEXT("%s: %d EFAM families, %d AENV affectors using %d distinct ids"), *Planet, Data.EnvironmentFamilies.Num(), Affectors.Num(), UsedIds.Num()));
		TArray<FSWGEnvironmentRow> TableRows;
		USWGAmbienceSubsystem::LoadEnvironmentTable(*Tre, Planet, TableRows);
		int32 FamiliesWithRow = 0;
		for (const FSWGEnvironmentFamily& Family : Data.EnvironmentFamilies)
		{
			const FSWGEnvironmentRow* Row = USWGAmbienceSubsystem::FindEnvironmentRow(TableRows, Family.Name, 0);
			if (Row && Row->Family.Equals(Family.Name, ESearchCase::IgnoreCase))
			{
				++FamiliesWithRow;
			}
			else
			{
				AddInfo(FString::Printf(TEXT("%s: EGRP family %s has no environment table row"), *Planet, *Family.Name));
			}
		}
		AddInfo(FString::Printf(TEXT("%s: %d of %d EGRP families have a table row"), *Planet, FamiliesWithRow, Data.EnvironmentFamilies.Num()));
	}
	TestTrue(TEXT("the planets parse"), PlanetsParsed >= 10);
	TestTrue(TEXT("the planets carry environment families"), PlanetsWithEnvironment >= 10);

	// Tatooine: 17 families, "global" is id 1, and Mos Eisley's city layer paints "moseisley" (id 7).
	FSWGTerrainData Tatooine;
	if (TestTrue(TEXT("tatooine parses"), FSWGTerrainReader::ReadTerrain(Tre->CreateIffReader(TEXT("terrain/tatooine.trn")), Tatooine)))
	{
		TestEqual(TEXT("tatooine EFAM count"), Tatooine.EnvironmentFamilies.Num(), 17);
		const FSWGEnvironmentFamily* Global = Tatooine.FindEnvironmentFamily(1);
		if (TestNotNull(TEXT("family 1"), Global))
		{
			TestEqual(TEXT("family 1 name"), Global->Name, FString(TEXT("global")));
			TestTrue(TEXT("family 1 swatch"), Global->Color == FColor(0xA9, 0xB3, 0x88));
		}
		if (const FSWGEnvironmentFamily* MosEisley = Tatooine.FindEnvironmentFamily(7))
		{
			TestEqual(TEXT("family 7 name"), MosEisley->Name, FString(TEXT("moseisley")));
		}

		TSet<int32> SeenIds;
		for (int32 StepY = -16; StepY <= 16; ++StepY)
		{
			for (int32 StepX = -16; StepX <= 16; ++StepX)
			{
				SeenIds.Add(FSWGTerrainEvaluator::GetEnvironmentFamily(Tatooine, StepX * 500.f, StepY * 500.f));
			}
		}
		AddInfo(FString::Printf(TEXT("tatooine environment ids over a 500 m grid: %d distinct"), SeenIds.Num()));
		TestTrue(TEXT("the grid finds more than one family"), SeenIds.Num() > 1);

		bool bFoundMosEisley = false;
		for (float OffsetX = -300.f; OffsetX <= 300.f && !bFoundMosEisley; OffsetX += 50.f)
		{
			for (float OffsetY = -300.f; OffsetY <= 300.f && !bFoundMosEisley; OffsetY += 50.f)
			{
				bFoundMosEisley = FSWGTerrainEvaluator::GetEnvironmentFamily(Tatooine, 3528.f + OffsetX, -4804.f + OffsetY) == 7;
			}
		}
		TestTrue(TEXT("Mos Eisley centre resolves to moseisley"), bFoundMosEisley);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGAmbienceTablesTest,
	"SWGEmu.Audio.Ambience.Tables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGAmbienceTablesTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForAmbienceTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// Every planet's environment table resolves, and every sound it names exists.
	int32 Tables = 0;
	int32 SoundReferences = 0;
	int32 MissingSounds = 0;
	for (const FString& Planet : FindEnvironmentTablePlanets(*Tre))
	{
		TArray<FSWGEnvironmentRow> Rows;
		if (!TestTrue(*FString::Printf(TEXT("%s environment table loads"), *Planet), USWGAmbienceSubsystem::LoadEnvironmentTable(*Tre, Planet, Rows)))
		{
			continue;
		}
		++Tables;
		TestNotNull(*FString::Printf(TEXT("%s resolves a default row"), *Planet), USWGAmbienceSubsystem::FindEnvironmentRow(Rows, TEXT("global"), 0));

		for (const FSWGEnvironmentRow& Row : Rows)
		{
			const FString* Sounds[] = { &Row.DayAmbient[0], &Row.DayAmbient[1], &Row.NightAmbient[0], &Row.NightAmbient[1], &Row.FirstMusic, &Row.SunriseMusic, &Row.SunsetMusic };
			for (const FString* Sound : Sounds)
			{
				if (Sound->IsEmpty())
				{
					continue;
				}
				++SoundReferences;
				if (!SoundExists(*Tre, *Sound))
				{
					++MissingSounds;
					AddWarning(FString::Printf(TEXT("%s family %s weather %d names %s, which is not in the TRE"), *Planet, *Row.Family, Row.WeatherIndex, **Sound));
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d environment tables, %d sound references, %d missing"), Tables, SoundReferences, MissingSounds));
	TestTrue(TEXT("environment tables are found"), Tables >= 20);
	// Retail data gaps: dathomir outpost2 (2 sounds, 2 weathers) and tatooine escapepod's music_event_explore (2 weathers).
	TestTrue(TEXT("only the known data gaps are missing"), MissingSounds <= 6);

	// interior.iff: sounds exist, room types are in the preset enum, and the rows match real POB cell names.
	const FSWGInteriorTable* Interior = FSWGInteriorTable::Get(*Tre);
	if (!TestNotNull(TEXT("interior table"), Interior))
	{
		return false;
	}
	TestEqual(TEXT("interior row count"), Interior->GetRows().Num(), 196);

	int32 NamedRows = 0;
	int32 NamedRowsWithPob = 0;
	int32 NamedRowsMatched = 0;
	int32 RowsWithoutPob = 0;
	TMap<FString, TSet<FString>> PobCellNames;
	TSet<FString> PobReadFailed;
	for (const FSWGInteriorRow& Row : Interior->GetRows())
	{
		for (const FString* Sound : { &Row.DayAmbient, &Row.NightAmbient, &Row.FirstMusic })
		{
			if (!Sound->IsEmpty() && !SoundExists(*Tre, *Sound))
			{
				AddError(FString::Printf(TEXT("interior %s/%s names %s, which is not in the TRE"), *Row.PobName, *Row.CellName, **Sound));
			}
		}
		TestTrue(*FString::Printf(TEXT("interior %s/%s room type %d is a preset"), *Row.PobName, *Row.CellName, Row.RoomType), Row.RoomType >= 0 && Row.RoomType < SWGReverbPresets::PresetCount);

		if (Row.PobName == TEXT("default"))
		{
			continue;
		}
		const FString PobPath = FString::Printf(TEXT("appearance/%s.pob"), *Row.PobName);
		if (!Tre->FileExists(PobPath))
		{
			++RowsWithoutPob;
			AddInfo(FString::Printf(TEXT("interior row %s/%s: no %s in the TRE"), *Row.PobName, *Row.CellName, *PobPath));
		}
		else if (!PobCellNames.Contains(Row.PobName))
		{
			FSWGPobData Pob;
			TSet<FString>& Names = PobCellNames.Add(Row.PobName);
			const bool bRead = FSWGPobReader::ReadPob(Tre->CreateIffReader(PobPath), Pob);
			for (const FSWGPobCell& Cell : Pob.Cells)
			{
				Names.Add(Cell.CellName.ToLower());
			}
			if (!bRead)
			{
				PobReadFailed.Add(Row.PobName);
			}
			if (!bRead)
			{
				AddInfo(FString::Printf(TEXT("%s: ReadPob failed after %d cell(s)"), *Row.PobName, Pob.Cells.Num()));
			}
		}

		if (Row.CellName != TEXT("default"))
		{
			++NamedRows;
			const TSet<FString>* Names = PobCellNames.Find(Row.PobName);
			if (Names && !PobReadFailed.Contains(Row.PobName))
			{
				++NamedRowsWithPob;
				if (Names->Contains(Row.CellName.ToLower()))
				{
					++NamedRowsMatched;
				}
				else
				{
					AddError(FString::Printf(TEXT("interior row %s/%s has no such cell in the POB"), *Row.PobName, *Row.CellName));
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("interior cell-name match: %d of %d rows with a POB (%d named rows in all; %d rows' POB is not in the TRE)"),
		NamedRowsMatched, NamedRowsWithPob, NamedRows, RowsWithoutPob));
	TestEqual(TEXT("every named row of a present POB matches a cell"), NamedRowsMatched, NamedRowsWithPob);

	// Fallback order: the cell's row, the building's default, the global default.
	FSWGInteriorTable::EMatch Match;
	if (const FSWGInteriorRow* Cantina = Interior->Find(TEXT("thm_tato_cantina"), TEXT("default"), &Match))
	{
		TestTrue(TEXT("cantina match"), Match == FSWGInteriorTable::EMatch::Cell);
		TestEqual(TEXT("cantina ambient"), Cantina->DayAmbient, FString(TEXT("sound/amb_cantina_medium_lp.snd")));
		TestEqual(TEXT("cantina room type is concertHall"), Cantina->RoomType, 7);
	}
	if (const FSWGInteriorRow* Lobby = Interior->Find(TEXT("mun_tato_capitol_s01"), TEXT("LOBBY"), &Match))
	{
		TestTrue(TEXT("lobby match"), Match == FSWGInteriorTable::EMatch::Cell);
		TestEqual(TEXT("lobby music"), Lobby->FirstMusic, FString(TEXT("sound/music_theme_tatooine.snd")));
	}
	Interior->Find(TEXT("mun_tato_capitol_s01"), TEXT("meeting1"), &Match);
	TestTrue(TEXT("unlisted room falls back to the building default"), Match == FSWGInteriorTable::EMatch::BuildingDefault);
	Interior->Find(TEXT("no_such_building"), TEXT("r1"), &Match);
	TestTrue(TEXT("unknown building falls back to the global default"), Match == FSWGInteriorTable::EMatch::GlobalDefault);

	// Reverb presets.
	UObject* Outer = GetTransientPackage();
	TSet<FString> PresetNames;
	for (int32 RoomType = 0; RoomType < SWGReverbPresets::PresetCount; ++RoomType)
	{
		PresetNames.Add(SWGReverbPresets::GetRoomTypeName(RoomType));
		UReverbEffect* Effect = SWGReverbPresets::GetReverbEffect(RoomType, Outer);
		if (TestNotNull(*FString::Printf(TEXT("reverb preset %d"), RoomType), Effect))
		{
			TestTrue(TEXT("decay time is in the engine's range"), Effect->DecayTime >= 0.1f && Effect->DecayTime <= 20.f);
			TestTrue(TEXT("effects are cached"), SWGReverbPresets::GetReverbEffect(RoomType, Outer) == Effect);
		}
	}
	TestEqual(TEXT("26 distinct preset names"), PresetNames.Num(), 26);
	TestEqual(TEXT("enum value 22"), FString(SWGReverbPresets::GetRoomTypeName(22)), FString(TEXT("sewerPipe")));
	TestEqual(TEXT("enum value 7"), FString(SWGReverbPresets::GetRoomTypeName(7)), FString(TEXT("concertHall")));
	TestNull(TEXT("out of range"), SWGReverbPresets::GetReverbEffect(26, Outer));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGSoundObjectTemplatesTest,
	"SWGEmu.Audio.Ambience.SoundObjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGSoundObjectTemplatesTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreForAmbienceTest(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	TestTrue(TEXT("soundobject template path"), ASWGSoundEmitter::IsSoundObjectTemplate(TEXT("object/soundobject/shared_soundobject_cantina_medium.iff")));
	TestFalse(TEXT("other template path"), ASWGSoundEmitter::IsSoundObjectTemplate(TEXT("object/static/structure/tatooine/shared_cantina.iff")));

	int32 Templates = 0;
	int32 WithLoop = 0;
	int32 MissingLoops = 0;
	int32 WithoutCdf = 0;
	for (const FString& Path : Tre->FindVirtualPaths(TEXT("object/soundobject/")))
	{
		if (!Path.EndsWith(TEXT(".iff")))
		{
			continue;
		}
		++Templates;

		FString CdfPath;
		if (!Tre->FindTemplateFormStringParam(Path, SWG_IFF_TAG('S', 'H', 'O', 'T'), TEXT("clientDataFile"), CdfPath))
		{
			AddInfo(FString::Printf(TEXT("%s names no clientDataFile"), *Path));
			++WithoutCdf;
			continue;
		}
		FSWGClientDataFile ClientData;
		if (!FSWGClientDataFileReader::Read(Tre->CreateIffReader(CdfPath), ClientData))
		{
			AddInfo(FString::Printf(TEXT("%s: %s is not in the TRE or does not parse"), *Path, *CdfPath));
			++WithoutCdf;
			continue;
		}

		bool bHasLoop = false;
		for (const FString& Loop : ClientData.AttachedSounds)
		{
			if (SoundExists(*Tre, Loop))
			{
				bHasLoop = true;
			}
			else
			{
				++MissingLoops;
				AddInfo(FString::Printf(TEXT("%s: ASND %s is not in the TRE"), *Path, *Loop));
			}
		}
		WithLoop += bHasLoop ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d sound object templates, %d with an existing ASND loop, %d ASND references missing"), Templates, WithLoop, MissingLoops));
	TestTrue(TEXT("65 sound object templates plus the base"), Templates >= 65);
	TestTrue(TEXT("only the base, city_crowd_cheering and waterfall_outdoors lack a usable cdf"), WithoutCdf <= 3);
	TestTrue(TEXT("nearly all have a loop"), WithLoop >= 60);

	for (const FString& Planet : { TEXT("tatooine"), TEXT("naboo"), TEXT("corellia"), TEXT("dathomir"), TEXT("talus"), TEXT("rori"), TEXT("lok"), TEXT("endor"), TEXT("yavin4"), TEXT("dantooine"), TEXT("dungeon1") })
	{
		FSWGWorldSnapshotData Snapshot;
		if (!FSWGWorldSnapshotReader::ReadWorldSnapshot(Tre->CreateIffReader(FString::Printf(TEXT("snapshot/%s.ws"), *Planet)), Snapshot))
		{
			AddError(FString::Printf(TEXT("snapshot/%s.ws does not parse"), *Planet));
			continue;
		}
		int32 Count = 0;
		TSet<FString> Used;
		CountSoundObjects(Snapshot, Snapshot.Nodes, Count, Used);
		AddInfo(FString::Printf(TEXT("%s.ws: %d sound object node(s), %d distinct template(s)"), *Planet, Count, Used.Num()));
	}
	return true;
}

#endif
