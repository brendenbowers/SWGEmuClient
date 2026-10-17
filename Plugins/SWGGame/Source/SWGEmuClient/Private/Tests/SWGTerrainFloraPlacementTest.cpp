#include "Misc/AutomationTest.h"
#include "TRE/SWGTerrainFloraPlacer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGTerrainFloraPlacementTest, "SWGEmu.Tre.Terrain.FloraPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGTerrainFloraPlacementTest::RunTest(const FString& Parameters)
{
	FSWGTerrainData Data;
	Data.Header.FloraCollidable.TileSize = 16.0f;
	FSWGFloraFamily& Family = Data.FloraFamilies.AddDefaulted_GetRef();
	Family.FamilyId = 1;
	Family.Children.AddDefaulted();
	FSWGTerrainLayer& Layer = Data.TopLevelLayers.AddDefaulted_GetRef();
	Layer.bEnabled = true;
	FSWGTerrainAffector& Affector = Layer.Affectors.AddDefaulted_GetRef();
	Affector.bEnabled = true;
	Affector.Type = ESWGTerrainAffectorType::FloraCollidable;
	Affector.FloraFamilyId = 1;
	Affector.FloraOperation = 1;

	for (float Border : {0.0f, 2.0f, 8.0f, 10.0f})
	{
		Data.Header.FloraCollidable.TileBorder = Border;
		TArray<FSWGTerrainFloraInstance> Instances, Reloaded;
		auto Place = [&](TArray<FSWGTerrainFloraInstance>& Out, float DensityScale)
		{
			FSWGTerrainFloraPlacer::PlaceCell(Data, {}, ESWGTerrainFloraTier::Collidable,
				FIntPoint(-1, 0), DensityScale, [](const FVector2D&) { return false; }, Out);
		};
		Place(Instances, 1.0f);
		Place(Reloaded, 1.0f);
		TestEqual(TEXT("full density fills the cell"), Instances.Num(), 64);
		TestEqual(TEXT("reload count"), Reloaded.Num(), Instances.Num());
		const float Inset = Border < 8.0f ? Border : 0.0f;
		bool bAwayFromCentre = false;
		for (int32 Index = 0; Index < Instances.Num(); ++Index)
		{
			const FVector& Position = Instances[Index].RawPosition;
			const double X = Position.X - FMath::FloorToDouble(Position.X / 16.0) * 16.0;
			const double Y = Position.Y - FMath::FloorToDouble(Position.Y / 16.0) * 16.0;
			TestTrue(TEXT("candidate stays inside inset"), X >= Inset && X <= 16.0 - Inset && Y >= Inset && Y <= 16.0 - Inset);
			bAwayFromCentre |= FMath::Abs(X - 8.0) > 2.0;
			if (Reloaded.IsValidIndex(Index)) TestEqual(TEXT("reload position"), Position, Reloaded[Index].RawPosition);
		}
		TestTrue(TEXT("zero or invalid border samples beyond centre jitter"), bAwayFromCentre);
		Instances.Reset();
		Place(Instances, 0.0f);
		TestTrue(TEXT("zero density scale fills nothing"), Instances.IsEmpty());
	}

	Family.bFloatsOnWater = true;
	Family.Children[0].bAlignToTerrain = true;
	Data.Header.bUseGlobalWaterTable = true;
	Data.Header.GlobalWaterTableHeight = 5.0f;
	TArray<FSWGTerrainFloraInstance> Floating;
	auto PlaceWaterCell = [&]()
	{
		Floating.Reset();
		FSWGTerrainFloraPlacer::PlaceCell(Data, {}, ESWGTerrainFloraTier::Collidable, FIntPoint(0, 0), 1.0f,
			[](const FVector2D&) { return false; }, Floating);
	};
	PlaceWaterCell();
	TestEqual(TEXT("floating flora keeps its density"), Floating.Num(), 64);
	for (const FSWGTerrainFloraInstance& Instance : Floating)
	{
		TestEqual(TEXT("floating flora sits at the water surface"), Instance.RawPosition.Z, 5.0);
		TestEqual(TEXT("floating flora stays upright"), Instance.RawNormal, FVector::UpVector);
	}
	Family.bFloatsOnWater = false;
	PlaceWaterCell();
	for (const FSWGTerrainFloraInstance& Instance : Floating) TestEqual(TEXT("ordinary flora stays on the ground"), Instance.RawPosition.Z, 0.0);
	Family.bFloatsOnWater = true;
	Data.Header.bUseGlobalWaterTable = false;
	PlaceWaterCell();
	for (const FSWGTerrainFloraInstance& Instance : Floating) TestEqual(TEXT("no water leaves flora on the ground"), Instance.RawPosition.Z, 0.0);
	Data.Header.bUseGlobalWaterTable = true;
	Data.Header.GlobalWaterTableHeight = -5.0f;
	PlaceWaterCell();
	for (const FSWGTerrainFloraInstance& Instance : Floating) TestEqual(TEXT("buried water does not lower flora"), Instance.RawPosition.Z, 0.0);
	Data.Header.bUseGlobalWaterTable = false;

	FSWGTerrainBoundary& Boundary = Layer.Boundaries.AddDefaulted_GetRef();
	Boundary.bEnabled = true;
	Boundary.Type = ESWGTerrainBoundaryType::Circle;
	Boundary.Radius = 10.0f;
	Boundary.FeatheringAmount = 1.0f;
	Family.Density = 0.5f;
	TestEqual(TEXT("family density at full strength"), FSWGTerrainEvaluator::GetFlora(Data, 0.0f, 0.0f, ESWGTerrainFloraTier::Collidable).Density, 0.5f);
	TestEqual(TEXT("feather strength multiplies density"), FSWGTerrainEvaluator::GetFlora(Data, 5.0f, 0.0f, ESWGTerrainFloraTier::Collidable).Density, 0.375f);
	Affector.bFloraDensityOverride = true;
	Affector.FloraDensity = 0.8f;
	TestEqual(TEXT("override is feathered too"), FSWGTerrainEvaluator::GetFlora(Data, 5.0f, 0.0f, ESWGTerrainFloraTier::Collidable).Density, 0.6f);
	return true;
}
#endif
