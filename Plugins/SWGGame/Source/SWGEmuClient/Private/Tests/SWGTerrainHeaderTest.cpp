#include "Misc/AutomationTest.h"

#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGTerrainEvaluator.h"
#include "TRE/SWGTerrainFloraPlacer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGTerrainHeaderTest,
	"SWGEmu.Tre.Terrain.Header",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGTerrainHeaderTest::RunTest(const FString& Parameters)
{
	UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
	USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
	if (!Tre)
	{
		AddError(TEXT("could not construct a TRE subsystem"));
		return false;
	}
	Tre->AddToRoot();
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };
	Tre->LoadConfig();
	if (!Tre->LoadArchives())
	{
		AddError(TEXT("no .tre archives loaded — check TreDirectory in DefaultGame.ini"));
		return false;
	}

	// Values confirmed by hex dump of the PTAT DATA chunk (2026-09-13).
	FSWGTerrainData Tatooine;
	TestTrue(TEXT("tatooine.trn parses"), FSWGTerrainReader::ReadTerrain(Tre->CreateIffReader(TEXT("terrain/tatooine.trn")), Tatooine));
	TestEqual(TEXT("map size"), Tatooine.Header.MapSize, 16384.0f);
	TestEqual(TEXT("chunk size"), Tatooine.Header.ChunkSize, 8.0f);
	TestEqual(TEXT("tiles per chunk"), Tatooine.Header.TilesPerChunk, 2u);
	TestEqual(TEXT("pole spacing"), Tatooine.Header.GetPoleSpacing(), 2.0f);
	TestFalse(TEXT("no global water"), Tatooine.Header.bUseGlobalWaterTable);
	TestEqual(TEXT("time cycle"), Tatooine.Header.TimeCycle, 11280.0f);
	TestEqual(TEXT("collidable flora min distance"), Tatooine.Header.FloraCollidable.MinDistance, 0.0f);
	TestEqual(TEXT("collidable flora max distance"), Tatooine.Header.FloraCollidable.MaxDistance, 128.0f);
	TestEqual(TEXT("collidable flora tile size"), Tatooine.Header.FloraCollidable.TileSize, 16.0f);
	TestEqual(TEXT("collidable flora tile border"), Tatooine.Header.FloraCollidable.TileBorder, 4.0f);
	TestEqual(TEXT("collidable flora seed"), Tatooine.Header.FloraCollidable.Seed, 0u);
	TestEqual(TEXT("non-collidable flora max distance"), Tatooine.Header.FloraNonCollidable.MaxDistance, 50.0f);
	TestEqual(TEXT("non-collidable flora tile border"), Tatooine.Header.FloraNonCollidable.TileBorder, 1.5f);
	TestEqual(TEXT("radial near max distance"), Tatooine.Header.RadialNear.MaxDistance, 28.0f);
	TestEqual(TEXT("radial near tile border"), Tatooine.Header.RadialNear.TileBorder, 0.48f);

	FSWGTerrainData Naboo;
	TestTrue(TEXT("naboo.trn parses"), FSWGTerrainReader::ReadTerrain(Tre->CreateIffReader(TEXT("terrain/naboo.trn")), Naboo));
	TestEqual(TEXT("naboo water height"), Naboo.Header.GlobalWaterTableHeight, -210.0f);
	TestEqual(TEXT("naboo water shader"), Naboo.Header.GlobalWaterTableShader, FString(TEXT("wter_nboo_beach")));

	// v0013: the legacy block sits between timeCycle and the vegetation tiers,
	// so the tiers only line up if it was consumed correctly.
	FSWGTerrainData CreatureTest;
	TestTrue(TEXT("creature_test.trn parses"), FSWGTerrainReader::ReadTerrain(Tre->CreateIffReader(TEXT("terrain/creature_test.trn")), CreatureTest));
	TestEqual(TEXT("v0013 map size"), CreatureTest.Header.MapSize, 4096.0f);
	TestTrue(TEXT("v0013 legacy block present"), CreatureTest.Header.LegacyV13.IsSet());
	if (CreatureTest.Header.LegacyV13.IsSet())
	{
		TestEqual(TEXT("v0013 legacy pairs"), CreatureTest.Header.LegacyV13->ShaderSizes.Num(), 4);
		TestEqual(TEXT("v0013 legacy size[1]"), CreatureTest.Header.LegacyV13->ShaderSizes[1], 0.7f);
		TestTrue(TEXT("v0013 legacy names empty"), CreatureTest.Header.LegacyV13->ShaderNames[0].IsEmpty());
	}
	TestEqual(TEXT("v0013 collidable flora max distance"), CreatureTest.Header.FloraCollidable.MaxDistance, 128.0f);
	TestEqual(TEXT("v0013 radial far tile size"), CreatureTest.Header.RadialFar.TileSize, 32.0f);
	TestFalse(TEXT("v0014 has no legacy block"), Tatooine.Header.LegacyV13.IsSet());

	FSWGTerrainData Corellia;
	TestTrue(TEXT("corellia.trn parses"), FSWGTerrainReader::ReadTerrain(Tre->CreateIffReader(TEXT("terrain/corellia.trn")), Corellia));
	TestTrue(TEXT("corellia has global water"), Corellia.Header.bUseGlobalWaterTable);

	// FGRP/RGRP families, values from a standalone decode of the same chunks (2026-09-18).
	TestEqual(TEXT("tatooine flora families"), Tatooine.FloraFamilies.Num(), 11);
	if (const FSWGFloraFamily* Hubba = Tatooine.FindFloraFamily(1))
	{
		TestEqual(TEXT("hubba name"), Hubba->Name, FString(TEXT("Hubba")));
		TestEqual(TEXT("hubba density"), Hubba->Density, 1.0f);
		TestEqual(TEXT("hubba children"), Hubba->Children.Num(), 3);
		if (Hubba->Children.Num() == 3)
		{
			TestEqual(TEXT("hubba child appearance"), Hubba->Children[0].AppearanceName, FString(TEXT("succ_tatt_hubba_lrg.apt")));
			TestTrue(TEXT("hubba child scales"), Hubba->Children[0].bShouldScale);
			TestEqual(TEXT("hubba child min scale"), Hubba->Children[0].MinScale, 0.45f);
			TestEqual(TEXT("hubba child max scale"), Hubba->Children[0].MaxScale, 1.2f);
			TestFalse(TEXT("hubba child no sway"), Hubba->Children[0].bShouldSway);
		}
	}
	else
	{
		AddError(TEXT("tatooine flora family 1 (Hubba) missing"));
	}
	if (const FSWGFloraFamily* Jinapur = Tatooine.FindFloraFamily(2))
	{
		TestEqual(TEXT("jinapur density"), Jinapur->Density, 0.085f);
		TestTrue(TEXT("jinapur bush sways"), Jinapur->Children.Num() > 1 && Jinapur->Children[0].bShouldSway);
		TestTrue(TEXT("pika aligns to terrain"), Jinapur->Children.Num() > 1 && Jinapur->Children[1].bAlignToTerrain);
	}
	TestEqual(TEXT("tatooine radial families"), Tatooine.RadialFamilies.Num(), 5);
	if (const FSWGRadialFamily* Grass = Tatooine.FindRadialFamily(1))
	{
		TestEqual(TEXT("grass name"), Grass->Name, FString(TEXT("dsrt_grss")));
		TestEqual(TEXT("grass children"), Grass->Children.Num(), 3);
		if (Grass->Children.Num() == 3)
		{
			TestEqual(TEXT("grass child shader"), Grass->Children[2].ShaderName, FString(TEXT("radl_grss_dsrt_tuft_a2")));
			TestEqual(TEXT("grass child weight"), Grass->Children[2].Weight, 2.0f);
			TestEqual(TEXT("grass child min width"), Grass->Children[2].MinWidth, 0.25f);
			TestEqual(TEXT("grass child max width"), Grass->Children[2].MaxWidth, 0.5f);
			TestTrue(TEXT("grass child keeps aspect"), Grass->Children[2].bMaintainAspectRatio);
			TestTrue(TEXT("grass child sways"), Grass->Children[2].bShouldSway);
		}
	}
	else
	{
		AddError(TEXT("tatooine radial family 1 (dsrt_grss) missing"));
	}

	// The affectors themselves: a coarse sweep of the map has to find every
	// tier growing somewhere, or the flora walk isn't reaching the affectors.
	// Tatooine's only placing far-radial affector is the oasis palms, which a
	// 500 m grid misses; Naboo has dozens.
	int32 SamplesWithFlora[(int32)ESWGTerrainFloraTier::Count] = {};
	for (int32 TierIndex = 0; TierIndex < (int32)ESWGTerrainFloraTier::Count; ++TierIndex)
	{
		const ESWGTerrainFloraTier Tier = (ESWGTerrainFloraTier)TierIndex;
		const FSWGTerrainData& Planet = Tier == ESWGTerrainFloraTier::RadialFar ? Naboo : Tatooine;
		for (int32 StepY = -16; StepY <= 16; ++StepY)
		{
			for (int32 StepX = -16; StepX <= 16; ++StepX)
			{
				const FSWGTerrainFloraSample Sample = FSWGTerrainEvaluator::GetFlora(Planet, StepX * 500.0f, StepY * 500.0f, Tier);
				if (Sample.FamilyId != 0 && Sample.Density > 0.0f)
				{
					++SamplesWithFlora[TierIndex];
				}
			}
		}
	}
	TestTrue(TEXT("collidable flora found"), SamplesWithFlora[(int32)ESWGTerrainFloraTier::Collidable] > 0);
	TestTrue(TEXT("non-collidable flora found"), SamplesWithFlora[(int32)ESWGTerrainFloraTier::NonCollidable] > 0);
	TestTrue(TEXT("near radial flora found"), SamplesWithFlora[(int32)ESWGTerrainFloraTier::RadialNear] > 0);
	TestTrue(TEXT("far radial flora found (naboo)"), SamplesWithFlora[(int32)ESWGTerrainFloraTier::RadialFar] > 0);
	AddInfo(FString::Printf(TEXT("33x33 sweep: %d/%d/%d samples with collidable/non-collidable/radial-near flora on tatooine, %d radial-far on naboo"),
		SamplesWithFlora[0], SamplesWithFlora[1], SamplesWithFlora[2], SamplesWithFlora[3]));

	// One placed cell, end to end: the deterministic placer over Tatooine's
	// collidable tier at the map centre should produce instances, every one
	// referencing a real family child and standing on the ground.
	TArray<FSWGTerrainFloraInstance> Instances;
	int32 CellsTried = 0;
	for (int32 CellY = -4; CellY <= 4 && Instances.IsEmpty(); ++CellY)
	{
		for (int32 CellX = -4; CellX <= 4 && Instances.IsEmpty(); ++CellX)
		{
			++CellsTried;
			FSWGTerrainFloraPlacer::PlaceCell(Tatooine, {}, ESWGTerrainFloraTier::Collidable, FIntPoint(CellX, CellY), 1.0f,
				[](const FVector2D&) { return false; }, Instances);
		}
	}
	TestTrue(TEXT("placer produced collidable instances near the map centre"), Instances.Num() > 0);
	for (const FSWGTerrainFloraInstance& Instance : Instances)
	{
		const FSWGFloraFamily* Family = Tatooine.FindFloraFamily(Instance.FamilyId);
		if (!Family || !Family->Children.IsValidIndex(Instance.ChildIndex))
		{
			AddError(FString::Printf(TEXT("instance references family %d child %d, which doesn't exist"), Instance.FamilyId, Instance.ChildIndex));
			break;
		}
		const float GroundHeight = FSWGTerrainEvaluator::GetHeight(Tatooine, Instance.RawPosition.X, Instance.RawPosition.Y);
		if (!FMath::IsNearlyEqual(GroundHeight, (float)Instance.RawPosition.Z, 0.01f))
		{
			AddError(FString::Printf(TEXT("instance at (%.1f, %.1f) sits at %.2f but the ground is %.2f"), Instance.RawPosition.X, Instance.RawPosition.Y, Instance.RawPosition.Z, GroundHeight));
			break;
		}
	}
	AddInfo(FString::Printf(TEXT("placer: %d collidable instance(s) in the first non-empty of %d cell(s)"), Instances.Num(), CellsTried));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
