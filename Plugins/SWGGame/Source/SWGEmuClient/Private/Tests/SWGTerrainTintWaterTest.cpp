#include "Misc/AutomationTest.h"

#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGTerrainEvaluator.h"
#include "TRE/SWGTerrainTint.h"
#include "TRE/SWGWaterRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Retail ground-tint maths and water rules (Docs/RetailTerrainNotes.md session 5). Synthetic data except the
// archive-backed reader check at the end.
namespace
{
	FSWGTerrainLayer& AddTintWaterLayer(FSWGTerrainData& Data)
	{
		FSWGTerrainLayer& Layer = Data.TopLevelLayers.AddDefaulted_GetRef();
		Layer.bEnabled = true;
		return Layer;
	}

	FSWGTerrainAffector& AddAffector(FSWGTerrainLayer& Layer, ESWGTerrainAffectorType Type)
	{
		FSWGTerrainAffector& Affector = Layer.Affectors.AddDefaulted_GetRef();
		Affector.bEnabled = true;
		Affector.Type = Type;
		return Affector;
	}

	FSWGTerrainBoundary& AddRectangleWater(FSWGTerrainLayer& Layer, float MinX, float MinY, float MaxX, float MaxY, float Height)
	{
		FSWGTerrainBoundary& Boundary = Layer.Boundaries.AddDefaulted_GetRef();
		Boundary.bEnabled = true;
		Boundary.Type = ESWGTerrainBoundaryType::Rectangle;
		Boundary.X0 = MinX;
		Boundary.Y0 = MinY;
		Boundary.X1 = MaxX;
		Boundary.Y1 = MaxY;
		Boundary.bLocalWaterTableEnabled = true;
		Boundary.LocalWaterTableHeight = Height;
		return Boundary;
	}

	FSWGTerrainColorRamp MakeRamp(std::initializer_list<FColor> Colors)
	{
		FSWGTerrainColorRamp Ramp;
		Ramp.Pixels = Colors;
		return Ramp;
	}

	bool SameColor(const FColor& A, const FColor& B) { return A.R == B.R && A.G == B.G && A.B == B.B; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGTerrainTintTest, "SWGEmu.Tre.Terrain.Tint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGTerrainTintTest::RunTest(const FString& Parameters)
{
	// 0xb8a4f0: add and subtract clamp per channel.
	TestTrue(TEXT("add clamps at 255"), SameColor(SWGTerrainTint::Blend(FColor(200, 100, 0), FColor(100, 100, 100), 1, 1.0f), FColor(255, 200, 100)));
	TestTrue(TEXT("subtract clamps at 0"), SameColor(SWGTerrainTint::Blend(FColor(50, 100, 200), FColor(100, 100, 100), 2, 1.0f), FColor(0, 0, 100)));
	// Below full amount the new colour is scaled (truncating) before add/subtract.
	TestTrue(TEXT("add scales the colour by the amount"), SameColor(SWGTerrainTint::Blend(FColor(10, 10, 10), FColor(100, 51, 7), 1, 0.5f), FColor(60, 35, 13)));
	TestTrue(TEXT("subtract scales the colour by the amount"), SameColor(SWGTerrainTint::Blend(FColor(100, 100, 100), FColor(100, 51, 7), 2, 0.5f), FColor(50, 75, 97)));
	// Operation 3 lerps towards the average of both, from the unscaled colour.
	TestTrue(TEXT("op 3 at full amount is the average"), SameColor(SWGTerrainTint::Blend(FColor(200, 0, 100), FColor(0, 100, 100), 3, 1.0f), FColor(100, 50, 100)));
	TestTrue(TEXT("op 3 at half amount is halfway to the average"), SameColor(SWGTerrainTint::Blend(FColor(200, 0, 100), FColor(0, 100, 100), 3, 0.5f), FColor(150, 25, 100)));
	// Default and operation 0 lerp towards the colour.
	TestTrue(TEXT("lerp at half amount"), SameColor(SWGTerrainTint::Blend(FColor(0, 100, 255), FColor(100, 0, 255), 0, 0.5f), FColor(50, 50, 255)));
	TestTrue(TEXT("unknown operations lerp too"), SameColor(SWGTerrainTint::Blend(FColor(0, 0, 0), FColor(100, 100, 100), 9, 1.0f), FColor(100, 100, 100)));
	TestTrue(TEXT("operation 4 is fatal in retail and leaves the colour"), SameColor(SWGTerrainTint::Blend(FColor(1, 2, 3), FColor(100, 100, 100), 4, 1.0f), FColor(1, 2, 3)));

	// 0xb8aca0 / ACRH index: trunc(fraction * (width - 1)).
	const FSWGTerrainColorRamp SampleRamp = MakeRamp({ FColor(10, 0, 0), FColor(20, 0, 0), FColor(30, 0, 0), FColor(40, 0, 0), FColor(50, 0, 0) });
	FColor Sampled;
	TestTrue(TEXT("fraction 0 samples the first pixel"), SWGTerrainTint::SampleRamp(SampleRamp, 0.0f, Sampled) && Sampled.R == 10);
	TestTrue(TEXT("fraction 0.49 truncates to pixel 1"), SWGTerrainTint::SampleRamp(SampleRamp, 0.49f, Sampled) && Sampled.R == 20);
	TestTrue(TEXT("fraction 1 samples the last pixel"), SWGTerrainTint::SampleRamp(SampleRamp, 1.0f, Sampled) && Sampled.R == 50);
	TestFalse(TEXT("fraction past 1 is outside the image"), SWGTerrainTint::SampleRamp(SampleRamp, 1.5f, Sampled));
	TestFalse(TEXT("negative fraction is outside the image"), SWGTerrainTint::SampleRamp(SampleRamp, -0.5f, Sampled));

	// TGA decoding: 24-bit bottom-up, two rows - the top (last stored) row wins; BGR becomes RGB.
	{
		TArray<uint8> Tga = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0 };
		Tga.Append({ 1, 2, 3, 4, 5, 6 });   // stored first = bottom row
		Tga.Append({ 30, 20, 10, 60, 50, 40 }); // stored last = top row
		FSWGTerrainColorRamp Decoded;
		TestTrue(TEXT("uncompressed TGA decodes"), SWGTerrainTint::DecodeTga(Tga, Decoded));
		TestEqual(TEXT("width"), Decoded.Pixels.Num(), 2);
		if (Decoded.Pixels.Num() == 2)
		{
			TestTrue(TEXT("top row, BGR swapped"), SameColor(Decoded.Pixels[0], FColor(10, 20, 30)) && SameColor(Decoded.Pixels[1], FColor(40, 50, 60)));
		}

		TArray<uint8> Rle = { 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 1, 0, 32, 0x20 };
		Rle.Append({ 0x82, 9, 8, 7, 255 }); // run of three
		FSWGTerrainColorRamp RunDecoded;
		TestTrue(TEXT("RLE TGA decodes"), SWGTerrainTint::DecodeTga(Rle, RunDecoded));
		TestTrue(TEXT("RLE run repeats the pixel"), RunDecoded.Pixels.Num() == 3 && SameColor(RunDecoded.Pixels[2], FColor(7, 8, 9)));

		TArray<uint8> Truncated = Tga;
		Truncated.SetNum(Truncated.Num() - 3);
		FSWGTerrainColorRamp Bad;
		TestFalse(TEXT("short pixel data is rejected"), SWGTerrainTint::DecodeTga(Truncated, Bad));
	}

	// Evaluator: constant colour inside a rectangle only.
	{
		FSWGTerrainData Data;
		FSWGTerrainLayer& Layer = AddTintWaterLayer(Data);
		FSWGTerrainBoundary& Boundary = Layer.Boundaries.AddDefaulted_GetRef();
		Boundary.bEnabled = true;
		Boundary.Type = ESWGTerrainBoundaryType::Rectangle;
		Boundary.X0 = 0.0f; Boundary.Y0 = 0.0f; Boundary.X1 = 100.0f; Boundary.Y1 = 100.0f;
		FSWGTerrainAffector& Constant = AddAffector(Layer, ESWGTerrainAffectorType::ColorConstant);
		Constant.ColorOperation = 0;
		Constant.ColorRgb = FColor(40, 80, 120);
		AddAffector(Layer, ESWGTerrainAffectorType::Exclude);

		FSWGTerrainGroundSample Inside, Outside;
		FSWGTerrainEvaluator::GetHeightAndGround(Data, 50.0f, 50.0f, Inside);
		FSWGTerrainEvaluator::GetHeightAndGround(Data, 500.0f, 500.0f, Outside);
		TestTrue(TEXT("tint inside the boundary"), SameColor(Inside.Tint, FColor(40, 80, 120)));
		TestTrue(TEXT("AEXC marks inside the boundary"), Inside.bExcluded);
		TestTrue(TEXT("white and unexcluded outside"), SameColor(Outside.Tint, FColor::White) && !Outside.bExcluded);
	}

	// Evaluator: ACRH reads the running height - the height affector before it counts, the one after does not.
	{
		FSWGTerrainData Data;
		Data.ColorRamps.Add(TEXT("terrain/test.tga"), MakeRamp({ FColor(0, 0, 0), FColor(100, 0, 0), FColor(200, 0, 0), FColor(250, 0, 0) }));
		FSWGTerrainLayer& Layer = AddTintWaterLayer(Data);
		FSWGTerrainAffector& Height = AddAffector(Layer, ESWGTerrainAffectorType::HeightConstant);
		Height.OperationType = 0;
		Height.Height = 15.0f;
		FSWGTerrainAffector& Ramp = AddAffector(Layer, ESWGTerrainAffectorType::ColorRampHeight);
		Ramp.ColorRampPath = TEXT("terrain/test.tga");
		Ramp.RampMinHeight = 10.0f;
		Ramp.RampMaxHeight = 20.0f;
		Ramp.ColorOperation = 0;
		FSWGTerrainAffector& Later = AddAffector(Layer, ESWGTerrainAffectorType::HeightConstant);
		Later.OperationType = 0;
		Later.Height = 500.0f;

		FSWGTerrainGroundSample Ground;
		const float FinalHeight = FSWGTerrainEvaluator::GetHeightAndGround(Data, 1.0f, 1.0f, Ground);
		TestEqual(TEXT("height matches GetHeight"), FinalHeight, FSWGTerrainEvaluator::GetHeight(Data, 1.0f, 1.0f));
		// Height 15 in [10,20] -> 0.5 * (4 - 1) = pixel 1 (100); lerp at amount 1 replaces white.
		TestTrue(TEXT("ramp sampled at the height of that moment"), SameColor(Ground.Tint, FColor(100, 0, 0)));

		// Outside the band the affector does nothing.
		Data.TopLevelLayers[0].Affectors[0].Height = 25.0f;
		FSWGTerrainEvaluator::GetHeightAndGround(Data, 1.0f, 1.0f, Ground);
		TestTrue(TEXT("height above max skips the ramp"), SameColor(Ground.Tint, FColor::White));
	}

	// Evaluator: ACRF indexes the ramp with the fractal; a ramp that failed to load is skipped.
	{
		FSWGTerrainData Data;
		FSWGMapFractal Fractal;
		Fractal.Seed = 77;
		Fractal.Octaves = 1;
		Fractal.OctavesParam = 2.0f;
		Fractal.Amplitude = 0.5f;
		Fractal.XFrequency = 1.0f;
		Fractal.YFrequency = 1.0f;
		Fractal.Combination = 1;
		Fractal.Initialize();
		Data.MapGroup.Fractals.Add(7, Fractal);
		Data.ColorRamps.Add(TEXT("terrain/fractal.tga"), MakeRamp({ FColor(0, 0, 0), FColor(50, 50, 50), FColor(100, 100, 100), FColor(150, 150, 150), FColor(200, 200, 200), FColor(250, 250, 250) }));

		FSWGTerrainLayer& Layer = AddTintWaterLayer(Data);
		FSWGTerrainAffector& Ramp = AddAffector(Layer, ESWGTerrainAffectorType::ColorRampFractal);
		Ramp.FractalId = 7;
		Ramp.ColorRampPath = TEXT("terrain/fractal.tga");
		Ramp.ColorOperation = 0;
		FSWGTerrainAffector& Missing = AddAffector(Layer, ESWGTerrainAffectorType::ColorRampFractal);
		Missing.FractalId = 7;
		Missing.ColorRampPath = TEXT("terrain/not_loaded.tga");

		FSWGTerrainGroundSample Ground;
		FSWGTerrainEvaluator::GetHeightAndGround(Data, 3.37f, 5.81f, Ground);
		const int32 ExpectedPixel = FMath::TruncToInt(Data.MapGroup.FindFractal(7)->GetNoise(3.37f, 5.81f) * 5);
		TestEqual(TEXT("ramp pixel follows the fractal"), static_cast<int32>(Ground.Tint.R), ExpectedPixel * 50);
	}

	// Reader against the shipped data: naboo carries all four affector kinds, and its beach ramp decodes.
	UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
	USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
	if (Tre)
	{
		Tre->AddToRoot();
		ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };
		Tre->LoadConfig();
		if (Tre->LoadArchives())
		{
			FSWGTerrainData Naboo;
			TestTrue(TEXT("naboo.trn parses"), FSWGTerrainReader::ReadTerrain(Tre->CreateIffReader(TEXT("terrain/naboo.trn")), Naboo));

			int32 Constants = 0, HeightRamps = 0, FractalRamps = 0, Excludes = 0;
			TFunction<void(const FSWGTerrainLayer&)> Count = [&](const FSWGTerrainLayer& Layer)
			{
				for (const FSWGTerrainAffector& Affector : Layer.Affectors)
				{
					Constants += Affector.Type == ESWGTerrainAffectorType::ColorConstant;
					HeightRamps += Affector.Type == ESWGTerrainAffectorType::ColorRampHeight;
					FractalRamps += Affector.Type == ESWGTerrainAffectorType::ColorRampFractal;
					Excludes += Affector.Type == ESWGTerrainAffectorType::Exclude;
				}
				for (const FSWGTerrainLayer& Child : Layer.Children) { Count(Child); }
			};
			for (const FSWGTerrainLayer& Layer : Naboo.TopLevelLayers) { Count(Layer); }
			TestEqual(TEXT("naboo ACCN count"), Constants, 5);
			TestEqual(TEXT("naboo ACRH count"), HeightRamps, 1);
			TestEqual(TEXT("naboo ACRF count"), FractalRamps, 89);
			TestEqual(TEXT("naboo AEXC count"), Excludes, 2);

			TArray<FString> Paths;
			SWGTerrainTint::CollectRampPaths(Naboo, Paths);
			TestTrue(TEXT("ramp paths use forward slashes"), Paths.Num() > 0 && !Paths.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("\\")); }));
			FSWGTerrainColorRamp Beach;
			TestTrue(TEXT("grasslands ramp decodes"), SWGTerrainTint::DecodeTga(Tre->ExtractFile(TEXT("terrain/naboo_colorramp_grasslands.tga")), Beach));
			TestEqual(TEXT("grasslands ramp is 64 wide"), Beach.Pixels.Num(), 64);
			if (Beach.Pixels.Num() == 64)
			{
				TestTrue(TEXT("first pixel"), SameColor(Beach.Pixels[0], FColor(146, 162, 143)));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGWaterRulesTest, "SWGEmu.Tre.Terrain.Water",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGWaterRulesTest::RunTest(const FString& Parameters)
{
	// Water table: the highest of the global table and every local boundary reaching the point (0xb6e8c0).
	{
		FSWGTerrainData Data;
		float Height = 0.0f;
		TestFalse(TEXT("no table, no water"), FSWGTerrainEvaluator::GetWaterHeight(Data, 0.0f, 0.0f, Height));

		FSWGTerrainLayer& Layer = AddTintWaterLayer(Data);
		AddRectangleWater(Layer, 0.0f, 0.0f, 100.0f, 100.0f, 12.0f);
		TestTrue(TEXT("local boundary alone"), FSWGTerrainEvaluator::GetWaterHeight(Data, 50.0f, 50.0f, Height) && Height == 12.0f);
		TestFalse(TEXT("outside the boundary"), FSWGTerrainEvaluator::GetWaterHeight(Data, 150.0f, 50.0f, Height));

		Data.Header.bUseGlobalWaterTable = true;
		Data.Header.GlobalWaterTableHeight = -5.0f;
		TestTrue(TEXT("global table everywhere"), FSWGTerrainEvaluator::GetWaterHeight(Data, 150.0f, 50.0f, Height) && Height == -5.0f);
		TestTrue(TEXT("higher local lake wins over the global table"), FSWGTerrainEvaluator::GetWaterHeight(Data, 50.0f, 50.0f, Height) && Height == 12.0f);

		FSWGTerrainLayer& Child = Layer.Children.AddDefaulted_GetRef();
		Child.bEnabled = true;
		AddRectangleWater(Child, 40.0f, 40.0f, 60.0f, 60.0f, 20.0f);
		TestTrue(TEXT("nested boundaries are found, highest wins"), FSWGTerrainEvaluator::GetWaterHeight(Data, 50.0f, 50.0f, Height) && Height == 20.0f);

		Data.TopLevelLayers[0].Boundaries[0].bEnabled = false;
		TestTrue(TEXT("disabled boundaries are ignored"), FSWGTerrainEvaluator::GetWaterHeight(Data, 10.0f, 10.0f, Height) && Height == -5.0f);

		// Polygon: a triangle.
		FSWGTerrainLayer& PolygonLayer = AddTintWaterLayer(Data);
		FSWGTerrainBoundary& Polygon = PolygonLayer.Boundaries.AddDefaulted_GetRef();
		Polygon.bEnabled = true;
		Polygon.Type = ESWGTerrainBoundaryType::Polygon;
		Polygon.Vertices = { FVector2D(200.0f, 0.0f), FVector2D(300.0f, 0.0f), FVector2D(250.0f, 100.0f) };
		Polygon.bLocalWaterTableEnabled = true;
		Polygon.LocalWaterTableHeight = 30.0f;
		TestTrue(TEXT("inside the polygon"), FSWGTerrainEvaluator::GetWaterHeight(Data, 250.0f, 30.0f, Height) && Height == 30.0f);
		TestTrue(TEXT("outside the polygon falls back to the global table"), FSWGTerrainEvaluator::GetWaterHeight(Data, 205.0f, 90.0f, Height) && Height == -5.0f);
	}

	// Rules.
	TestTrue(TEXT("strictly below the surface is in water"), SWGWaterRules::IsInWater(true, 10.0f, 9.99f));
	TestFalse(TEXT("on the surface is not"), SWGWaterRules::IsInWater(true, 10.0f, 10.0f));
	TestFalse(TEXT("no table is never in water"), SWGWaterRules::IsInWater(false, 10.0f, -100.0f));

	// Swim height 1: ground 8 under a 10 surface swims, ground 9.5 wades, ground 11 is dry.
	TestTrue(TEXT("deep water swims"), SWGWaterRules::IsSwimming(true, 10.0f, 8.0f, 1.0f));
	TestFalse(TEXT("exactly swimHeight deep does not yet swim"), SWGWaterRules::IsSwimming(true, 10.0f, 9.0f, 1.0f));
	TestFalse(TEXT("shallow water does not swim"), SWGWaterRules::IsSwimming(true, 10.0f, 9.5f, 1.0f));
	TestTrue(TEXT("shallow water wades"), SWGWaterRules::IsWading(true, 10.0f, 9.5f, 1.0f));
	TestFalse(TEXT("deep water is not wading"), SWGWaterRules::IsWading(true, 10.0f, 8.0f, 1.0f));
	TestFalse(TEXT("dry ground neither swims nor wades"), SWGWaterRules::IsSwimming(true, 10.0f, 11.0f, 1.0f) || SWGWaterRules::IsWading(true, 10.0f, 11.0f, 1.0f));
	TestFalse(TEXT("no table never swims"), SWGWaterRules::IsSwimming(false, 10.0f, -50.0f, 1.0f));

	// Core3's rule trips 0.2 m earlier than the client flag; they agree once clearly deep or clearly dry.
	TestTrue(TEXT("server swims in the 0.2 m band the client does not"), SWGWaterRules::ServerWouldSwim(true, 10.0f, 8.9f, 1.0f) && !SWGWaterRules::IsSwimming(true, 10.0f, 8.9f + 0.19f, 1.0f));
	TestTrue(TEXT("both swim when deep"), SWGWaterRules::ServerWouldSwim(true, 10.0f, 5.0f, 1.0f) && SWGWaterRules::IsSwimming(true, 10.0f, 5.0f, 1.0f));
	TestFalse(TEXT("server stays dry above the surface"), SWGWaterRules::ServerWouldSwim(true, 10.0f, 12.0f, 1.0f));

	TestEqual(TEXT("swimmer floats swimHeight under the surface"), SWGWaterRules::GetSwimmerHeight(true, 10.0f, 5.0f, 1.0f), 9.0f);
	TestEqual(TEXT("a wader stays on the ground"), SWGWaterRules::GetSwimmerHeight(true, 10.0f, 9.5f, 1.0f), 9.5f);
	TestEqual(TEXT("depth over the ground"), SWGWaterRules::GetDepth(true, 10.0f, 4.0f), 6.0f);
	TestEqual(TEXT("no depth on dry land"), SWGWaterRules::GetDepth(true, 10.0f, 14.0f), 0.0f);

	TestTrue(TEXT("retail underwater colour"), SameColor(SWGWaterRules::GetUnderwaterColor(), FColor(0x4d, 0x73, 0x71)));
	TestEqual(TEXT("retail underwater far plane"), SWGWaterRules::UnderwaterFarPlane, 500.0f);
	return true;
}

#endif
