#include "Misc/AutomationTest.h"

#include "Common/SWGFloraParticles.h"
#include "Engine/GameInstance.h"
#include "Objects/World/SWGParticleEmitterActor.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGAshReader.h"
#include "TRE/SWGLatReader.h"
#include "TRE/SWGMeshReader.h"
#include "TRE/SWGParticleEffectReader.h"
#include "TRE/SWGTerrainFloraPlacer.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGWorldSnapshotReader.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Stage 6: world particle objects, flora particle children and ambient .sat mesh particles resolve in the real data. */
namespace
{
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
			Test.AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
			Tre->RemoveFromRoot();
			return nullptr;
		}
		return Tre;
	}

	TArray<FString> FindFilesWithExtension(const USWGTreSubsystem& Tre, const TCHAR* Extension)
	{
		TArray<FString> Paths = Tre.FindVirtualPaths(Extension);
		Paths.RemoveAll([Extension](const FString& Path) { return !Path.EndsWith(Extension, ESearchCase::IgnoreCase); });
		Paths.Sort();
		return Paths;
	}

	void CollectTemplates(const FSWGWorldSnapshotData& Snapshot, const TArray<FSWGWorldSnapshotNode>& Nodes, TMap<FString, int32>& OutCounts)
	{
		for (const FSWGWorldSnapshotNode& Node : Nodes)
		{
			if (Snapshot.ObjectTemplateNames.IsValidIndex(static_cast<int32>(Node.NameID)))
			{
				OutCounts.FindOrAdd(Snapshot.ObjectTemplateNames[Node.NameID]) += 1;
			}
			CollectTemplates(Snapshot, Node.Children, OutCounts);
		}
	}

	FString FirstString(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk)
	{
		return FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Reader.GetChunkData(Chunk))));
	}

	/** SMAT/0003: the first MSGN mesh path and LATX's skeleton -> lat pair (the chain ResolveAppearanceMeshPaths walks). */
	bool ReadSatChain(const USWGTreSubsystem& Tre, const FString& SatPath, FString& OutMeshPath, FString& OutSkeletonPath, FString& OutLatPath)
	{
		const FSWGIffReader Reader = Tre.CreateIffReader(SatPath);
		FSWGIffChunk Smat, Version, Msgn, Latx;
		if (!Reader.FindForm(SWG_IFF_TAG('S', 'M', 'A', 'T'), Smat) || !Reader.FindChildForm(Smat, SWG_IFF_TAG('0', '0', '0', '3'), Version)
			|| !Reader.FindChildChunk(Version, SWG_IFF_TAG('M', 'S', 'G', 'N'), Msgn) || !Reader.FindChildChunk(Version, SWG_IFF_TAG('L', 'A', 'T', 'X'), Latx))
		{
			return false;
		}

		OutMeshPath = FirstString(Reader, Msgn);
		const uint8* Data = Reader.GetChunkData(Latx);
		const int32 Size = Reader.GetChunkSize(Latx);
		if (Size < 4 || (Data[0] | (Data[1] << 8)) < 1)
		{
			return false;
		}
		OutSkeletonPath = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Data + 2)));
		OutLatPath = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Data + 2 + OutSkeletonPath.Len() + 1)));
		return !OutMeshPath.IsEmpty() && !OutSkeletonPath.IsEmpty() && !OutLatPath.IsEmpty();
	}

	FString PrtFilePath(const FString& Name)
	{
		return Name.Contains(TEXT("/")) ? Name : TEXT("appearance/") + Name;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGWorldParticleObjectsTest,
	"SWGEmu.Fx.WorldParticles.Objects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGWorldParticleObjectsTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreSubsystem(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	TestTrue(TEXT("prefix matches"), ASWGParticleEmitterActor::IsParticleObjectTemplate(TEXT("object/static/particle/shared_particle_smoke.iff")));
	TestFalse(TEXT("prefix does not match props"), ASWGParticleEmitterActor::IsParticleObjectTemplate(TEXT("object/static/structure/tatooine/shared_streetlamp_tatt_s01.iff")));

	int32 TotalNodes = 0;
	TSet<FString> DistinctParticleTemplates, Checked, OtherParticleTemplates;
	for (const FString& SnapshotPath : FindFilesWithExtension(*Tre, TEXT(".ws")))
	{
		if (!SnapshotPath.StartsWith(TEXT("snapshot/")))
		{
			continue;
		}
		FSWGWorldSnapshotData Snapshot;
		if (!FSWGWorldSnapshotReader::ReadWorldSnapshot(Tre->CreateIffReader(SnapshotPath), Snapshot))
		{
			continue;
		}

		TMap<FString, int32> Counts;
		CollectTemplates(Snapshot, Snapshot.Nodes, Counts);
		int32 PlanetNodes = 0;
		for (const TPair<FString, int32>& Pair : Counts)
		{
			FString PrtPath;
			const bool bResolves = ASWGParticleEmitterActor::ResolveParticlePath(*Tre, Pair.Key, PrtPath);
			if (ASWGParticleEmitterActor::IsParticleObjectTemplate(Pair.Key))
			{
				PlanetNodes += Pair.Value;
				DistinctParticleTemplates.Add(Pair.Key);
				if (TestTrue(*(Pair.Key + TEXT(" has a .prt appearance")), bResolves))
				{
					// Retail ships this test template pointing at a .prt that is in no archive; the emitter then plays nothing.
					if (!Tre->FileExists(PrtPath))
					{
						TestEqual(TEXT("only the known-dangling test template is missing its .prt"), PrtPath, FString(TEXT("appearance/particle_test_31.prt")));
						UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.WorldParticles.Objects] %s points at missing %s"), *Pair.Key, *PrtPath);
					}
				}
			}
			else if (bResolves && !Checked.Contains(Pair.Key))
			{
				// Sound objects and STATs (waterfalls) with a .prt appearance are routed by their form, not the path prefix.
				OtherParticleTemplates.Add(Pair.Key);
				TestTrue(*(PrtPath + TEXT(" exists (from ") + Pair.Key + TEXT(")")), Tre->FileExists(PrtPath));
			}
			Checked.Add(Pair.Key);
		}
		TotalNodes += PlanetNodes;
		UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.WorldParticles.Objects] %s: %d particle object node(s)"), *SnapshotPath, PlanetNodes);
	}

	UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.WorldParticles.Objects] %d particle object node(s), %d distinct template(s)"), TotalNodes, DistinctParticleTemplates.Num());
	int32 SoundObjects = 0, Others = 0;
	for (const FString& Template : OtherParticleTemplates)
	{
		(Template.StartsWith(TEXT("object/soundobject/")) ? SoundObjects : Others) += 1;
		if (!Template.StartsWith(TEXT("object/soundobject/")))
		{
			UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.WorldParticles.Objects] STAT with a .prt appearance outside the prefix: %s"), *Template);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.WorldParticles.Objects] outside the prefix: %d sound object template(s), %d other template(s)"), SoundObjects, Others);
	TestEqual(TEXT("particle objects placed by snapshots"), TotalNodes, 434);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGWorldParticleFloraTest,
	"SWGEmu.Fx.WorldParticles.Flora",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGWorldParticleFloraTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreSubsystem(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	int32 TotalChildren = 0, TotalRadial = 0, Missing = 0;
	for (const FString& TerrainPath : FindFilesWithExtension(*Tre, TEXT(".trn")))
	{
		if (!TerrainPath.StartsWith(TEXT("terrain/")))
		{
			continue;
		}
		FSWGTerrainData Terrain;
		if (!FSWGTerrainReader::ReadTerrain(Tre->CreateIffReader(TerrainPath), Terrain))
		{
			continue;
		}

		TMap<FString, int32> Names;
		int32 FamilyCount = 0;
		for (const FSWGFloraFamily& Family : Terrain.FloraFamilies)
		{
			bool bHasParticle = false;
			for (const FSWGFloraChild& Child : Family.Children)
			{
				if (Child.AppearanceName.EndsWith(TEXT(".prt"), ESearchCase::IgnoreCase))
				{
					Names.FindOrAdd(Child.AppearanceName) += 1;
					bHasParticle = true;
					++TotalChildren;
				}
			}
			FamilyCount += bHasParticle ? 1 : 0;
		}
		int32 RadialCount = 0;
		for (const FSWGRadialFamily& Family : Terrain.RadialFamilies)
		{
			for (const FSWGRadialChild& Child : Family.Children)
			{
				RadialCount += Child.ShaderName.EndsWith(TEXT(".prt"), ESearchCase::IgnoreCase) ? 1 : 0;
			}
		}
		TotalRadial += RadialCount;

		FString List;
		for (const TPair<FString, int32>& Pair : Names)
		{
			const bool bExists = Tre->FileExists(PrtFilePath(Pair.Key));
			Missing += bExists ? 0 : 1;
			TestTrue(*(TerrainPath + TEXT(" child ") + Pair.Key + TEXT(" exists")), bExists);
			List += FString::Printf(TEXT(" %s(x%d)"), *Pair.Key, Pair.Value);
		}
		// How many placements a real cell holds and how many survive the default thinning (for tuning the cvars).
		if (!Names.IsEmpty())
		{
			for (int32 TierIndex = 0; TierIndex < 2; ++TierIndex)
			{
				const ESWGTerrainFloraTier Tier = TierIndex == 0 ? ESWGTerrainFloraTier::Collidable : ESWGTerrainFloraTier::NonCollidable;
				int32 Cells = 0, Placed = 0, Particles = 0, Kept = 0;
				for (int32 CellY = -3; CellY <= 3; ++CellY)
				{
					for (int32 CellX = -3; CellX <= 3; ++CellX)
					{
						TArray<FSWGTerrainFloraInstance> Instances;
						FSWGTerrainFloraPlacer::PlaceCell(Terrain, TArrayView<const FSWGTerrainLayer>(), Tier, FIntPoint(CellX, CellY), 1.0f, [](const FVector2D&) { return false; }, Instances);
						TArray<SWGFloraParticles::FPlacement> Candidates;
						for (const FSWGTerrainFloraInstance& Instance : Instances)
						{
							const FSWGFloraFamily* Family = Terrain.FindFloraFamily(Instance.FamilyId);
							if (Family && Family->Children.IsValidIndex(Instance.ChildIndex) && Family->Children[Instance.ChildIndex].AppearanceName.EndsWith(TEXT(".prt")))
							{
								SWGFloraParticles::FPlacement& Placement = Candidates.AddDefaulted_GetRef();
								Placement.RawPosition = Instance.RawPosition;
							}
						}
						++Cells;
						Placed += Instances.Num();
						Particles += Candidates.Num();
						Kept += SWGFloraParticles::Sample(Candidates, SWGFloraParticles::FSampleSettings{ 1.f, 30.f, 128 }).Num();
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.WorldParticles.Flora] %s tier %d (cell %.0f m): %d cell(s) near the origin hold %d placement(s), %d .prt, %d kept at 30 m spacing"),
					*TerrainPath, TierIndex, FSWGTerrainFloraPlacer::GetCellSize(Terrain, Tier), Cells, Placed, Particles, Kept);
			}
		}

		if (!Names.IsEmpty() || RadialCount > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.WorldParticles.Flora] %s: %d FFAM famil(ies) with .prt children, %d RFAM .prt child(ren):%s"), *TerrainPath, FamilyCount, RadialCount, *List);
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.WorldParticles.Flora] %d FFAM .prt child(ren), %d RFAM .prt child(ren), %d missing"), TotalChildren, TotalRadial, Missing);
	TestTrue(TEXT("flora .prt children found"), TotalChildren > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGWorldParticleSamplerTest,
	"SWGEmu.Fx.WorldParticles.Sampler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGWorldParticleSamplerTest::RunTest(const FString& Parameters)
{
	// A dense 40 x 40 m grid, one candidate per metre.
	TArray<SWGFloraParticles::FPlacement> Candidates;
	for (int32 Row = 0; Row < 40; ++Row)
	{
		for (int32 Column = 0; Column < 40; ++Column)
		{
			SWGFloraParticles::FPlacement& Placement = Candidates.AddDefaulted_GetRef();
			Placement.PrtPath = TEXT("appearance/pt_test.prt");
			Placement.RawPosition = FVector(Column + 0.25f, Row + 0.5f, 10.f);
		}
	}

	SWGFloraParticles::FSampleSettings Settings;
	Settings.MinSpacingMeters = 10.f;
	Settings.MaxPerCell = 8;
	const TArray<int32> Kept = SWGFloraParticles::Sample(Candidates, Settings);
	TestEqual(TEXT("capped"), Kept.Num(), 8);
	for (int32 First = 0; First < Kept.Num(); ++First)
	{
		for (int32 Second = First + 1; Second < Kept.Num(); ++Second)
		{
			TestTrue(TEXT("spaced"), FVector2D::Distance(FVector2D(Candidates[Kept[First]].RawPosition), FVector2D(Candidates[Kept[Second]].RawPosition)) >= 10.f);
		}
	}
	TestTrue(TEXT("deterministic"), SWGFloraParticles::Sample(Candidates, Settings) == Kept);

	Settings.MaxPerCell = 1000;
	Settings.MinSpacingMeters = 0.f;
	Settings.Fraction = 0.25f;
	const int32 Quarter = SWGFloraParticles::Sample(Candidates, Settings).Num();
	TestTrue(TEXT("a quarter keeps about a quarter"), Quarter > 200 && Quarter < 600);

	Settings.Fraction = 0.f;
	TestEqual(TEXT("fraction 0 keeps none"), SWGFloraParticles::Sample(Candidates, Settings).Num(), 0);
	TestEqual(TEXT("no candidates"), SWGFloraParticles::Sample(TArray<SWGFloraParticles::FPlacement>(), SWGFloraParticles::FSampleSettings()).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGWorldParticleSkeletalTest,
	"SWGEmu.Fx.WorldParticles.Skeletal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGWorldParticleSkeletalTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreSubsystem(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// Every .sat a .prt draws as a mesh particle needs a mesh, skeleton and a root loop clip.
	TSet<FString> SatPaths;
	for (const FString& PrtPath : FindFilesWithExtension(*Tre, TEXT(".prt")))
	{
		FSWGParticleEffect Effect;
		if (!FSWGParticleEffectReader::Read(Tre->CreateIffReader(PrtPath), Effect))
		{
			continue;
		}
		TArray<FString> Shaders, Meshes, Children, Sounds;
		Effect.CollectReferences(Shaders, Meshes, Children, Sounds);
		for (const FString& MeshPath : Meshes)
		{
			if (MeshPath.EndsWith(TEXT(".sat"), ESearchCase::IgnoreCase))
			{
				SatPaths.Add(MeshPath);
			}
		}
	}

	int32 Ambient = 0;
	for (const FString& SatPath : FindFilesWithExtension(*Tre, TEXT(".sat")))
	{
		if (SatPath.StartsWith(TEXT("appearance/pt_ambient_")))
		{
			++Ambient;
			TestTrue(*(SatPath + TEXT(" is referenced by a .prt")), SatPaths.Contains(SatPath));
		}
	}

	for (const FString& SatPath : SatPaths)
	{
		FString MeshPath, SkeletonPath, LatPath;
		if (!TestTrue(*(SatPath + TEXT(" .sat reads")), ReadSatChain(*Tre, SatPath, MeshPath, SkeletonPath, LatPath)))
		{
			continue;
		}
		TestTrue(*(MeshPath + TEXT(" exists")), Tre->FileExists(MeshPath));
		TestTrue(*(SkeletonPath + TEXT(" exists")), Tre->FileExists(SkeletonPath));

		FSWGLatData Lat;
		FSWGAnimationStateHierarchy Hierarchy;
		if (!TestTrue(*(LatPath + TEXT(" reads")), FSWGLatReader::ReadLat(Tre->CreateIffReader(LatPath), Lat))
			|| !TestTrue(*(Lat.AshPath + TEXT(" reads")), FSWGAshReader::ReadHierarchy(Tre->CreateIffReader(Lat.AshPath), Hierarchy)))
		{
			continue;
		}
		const FSWGAnimationState* Root = Hierarchy.GetRoot();
		if (!TestNotNull(*(Lat.AshPath + TEXT(" root state")), Root))
		{
			continue;
		}
		const FSWGLatEntry* Entry = Lat.Find(Root->LoopAnimationName);
		if (TestNotNull(*(LatPath + TEXT(" has the root loop ") + Root->LoopAnimationName), Entry))
		{
			TestTrue(*(LatPath + TEXT(" loop has a clip")), !Entry->Clips.IsEmpty());
			for (const FSWGLatClip& Clip : Entry->Clips)
			{
				TestTrue(*(Clip.AnsPath + TEXT(" exists")), Tre->FileExists(Clip.AnsPath));
			}
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[SWGEmu.Fx.WorldParticles.Skeletal] %d .sat mesh particle(s) referenced by .prt files, %d pt_ambient_*.sat"), SatPaths.Num(), Ambient);
	TestTrue(TEXT("ambient animals found"), Ambient >= 20);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
