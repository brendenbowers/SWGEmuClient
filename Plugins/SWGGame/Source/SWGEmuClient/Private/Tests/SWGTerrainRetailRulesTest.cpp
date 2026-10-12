#include "Misc/AutomationTest.h"
#include "TRE/SWGTerrainEvaluator.h"

#if WITH_DEV_AUTOMATION_TESTS
// Synthetic terrain, no TRE dependency: pins the rules recovered from SWGEmu.exe
// (see Docs/RetailTerrainNotes.md, session 3) so a regression cannot slip back to Core3's variants.
namespace
{
	FSWGMapFractal MakeLatticeFractal(int32 Combination)
	{
		FSWGMapFractal Fractal;
		Fractal.Seed = 1234;
		Fractal.Octaves = 1;
		Fractal.OctavesParam = 2.0f;
		Fractal.Amplitude = 0.5f;
		Fractal.XFrequency = 1.0f;
		Fractal.YFrequency = 1.0f;
		Fractal.Combination = Combination;
		Fractal.Initialize();
		return Fractal;
	}

	FSWGTerrainAffector& AddHeightConstant(FSWGTerrainLayer& Layer, int32 Operation, float Height)
	{
		FSWGTerrainAffector& Affector = Layer.Affectors.AddDefaulted_GetRef();
		Affector.bEnabled = true;
		Affector.Type = ESWGTerrainAffectorType::HeightConstant;
		Affector.OperationType = Operation;
		Affector.Height = Height;
		return Affector;
	}

	FSWGTerrainLayer& AddLayer(FSWGTerrainData& Data)
	{
		FSWGTerrainLayer& Layer = Data.TopLevelLayers.AddDefaulted_GetRef();
		Layer.bEnabled = true;
		return Layer;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGTerrainRetailRulesTest, "SWGEmu.Tre.Terrain.RetailRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGTerrainRetailRulesTest::RunTest(const FString& Parameters)
{
	// MultiFractal (0x13e7c00): Perlin noise is exactly zero on the integer lattice, so each
	// combination rule reduces to a constant there (single octave, unit scale).
	const float ExpectedAtLattice[] = { 0.5f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f };
	for (int32 Combination = 0; Combination < 6; ++Combination)
	{
		TestEqual(*FString::Printf(TEXT("combination %d at a lattice point"), Combination), MakeLatticeFractal(Combination).GetNoise(3.0f, 5.0f), ExpectedAtLattice[Combination], 1e-5f);
	}
	FSWGMapFractal Plain = MakeLatticeFractal(0);
	const float Sampled = Plain.GetNoise(3.37f, 5.81f);
	TestTrue(TEXT("off-lattice noise stays inside (0,1) and differs from the lattice value"), Sampled > 0.0f && Sampled < 1.0f && !FMath::IsNearlyEqual(Sampled, 0.5f, 1e-4f));
	FSWGMapFractal Shaped = MakeLatticeFractal(0);
	Shaped.Bias = 1;
	Shaped.BiasValue = 0.5f;
	Shaped.GainType = 1;
	Shaped.GainValue = 0.7f;
	TestEqual(TEXT("bias 0.5 and gain are fixed points at 0.5"), Shaped.GetNoise(3.0f, 5.0f), 0.5f, 1e-4f);
	FSWGMapFractal Clamped = MakeLatticeFractal(3);
	Clamped.GainType = 1;
	TestEqual(TEXT("gain snaps values under 0.001 to 0"), Clamped.GetNoise(3.0f, 5.0f), 0.0f);
	Clamped.Combination = 2;
	TestEqual(TEXT("gain snaps values over 0.999 to 1"), Clamped.GetNoise(3.0f, 5.0f), 1.0f);

	// AffectorHeightFractal (0xb768a0), amount 1: the fractal is 0.5 at (3,5), times scale 4 = 2.
	FSWGTerrainData FractalData;
	FractalData.MapGroup.Fractals.Add(1, MakeLatticeFractal(0));
	const struct { int32 Operation; float Expected; } FractalOps[] = { { 0, 2.0f }, { 1, 12.0f }, { 2, 8.0f }, { 3, 20.0f } };
	for (const auto& Case : FractalOps)
	{
		FSWGTerrainData Data = FractalData;
		FSWGTerrainLayer& Layer = AddLayer(Data);
		AddHeightConstant(Layer, 0, 10.0f);
		FSWGTerrainAffector& Fractal = Layer.Affectors.AddDefaulted_GetRef();
		Fractal.bEnabled = true;
		Fractal.Type = ESWGTerrainAffectorType::HeightFractal;
		Fractal.FractalId = 1;
		Fractal.Height = 4.0f;
		Fractal.OperationType = Case.Operation;
		TestEqual(*FString::Printf(TEXT("fractal operation %d"), Case.Operation), FSWGTerrainEvaluator::GetHeight(Data, 3.0f, 5.0f), Case.Expected, 1e-3f);
	}

	// FilterFractal (0xb77be0): passes inside (min,max), zero outside.
	for (const auto& Case : { TPair<float, float>(0.4f, 5.0f), TPair<float, float>(0.6f, 0.0f) })
	{
		FSWGTerrainData Data = FractalData;
		FSWGTerrainLayer& Layer = AddLayer(Data);
		AddHeightConstant(Layer, 0, 5.0f);
		FSWGTerrainFilter& Filter = Layer.Filters.AddDefaulted_GetRef();
		Filter.bEnabled = true;
		Filter.Type = ESWGTerrainFilterType::Fractal;
		Filter.FractalId = 1;
		Filter.Scale = 1.0f;
		Filter.Min = Case.Key;
		Filter.Max = Case.Key + 0.2f;
		TestEqual(TEXT("fractal filter band"), FSWGTerrainEvaluator::GetHeight(Data, 3.0f, 5.0f), Case.Value, 1e-3f);
	}

	// AffectorHeightTerrace (0xb770b0): the modulus is floored below zero, unlike Core3 (which gives 0 for -3).
	const struct { float Start; float Expected; } TerraceCases[] = { { -3.0f, -4.0f }, { 22.0f, 20.0f }, { 30.0f, 20.0f + 20.0f * 5.0f / 15.0f } };
	for (const auto& Case : TerraceCases)
	{
		FSWGTerrainData Data;
		FSWGTerrainLayer& Layer = AddLayer(Data);
		AddHeightConstant(Layer, 0, Case.Start);
		FSWGTerrainAffector& Terrace = Layer.Affectors.AddDefaulted_GetRef();
		Terrace.bEnabled = true;
		Terrace.Type = ESWGTerrainAffectorType::HeightTerrace;
		Terrace.Height = 20.0f;
		Terrace.FlatRatio = 0.25f;
		TestEqual(*FString::Printf(TEXT("terrace from %.0f"), Case.Start), FSWGTerrainEvaluator::GetHeight(Data, 0.0f, 0.0f), Case.Expected, 1e-3f);
	}

	// ASCN / ASRP (0xb87410 / 0xb878e0): hard single-family paint gated by a threshold.
	FSWGTerrainData ShaderData;
	ShaderData.MapGroup.ShaderThresholds.Add(5, 0.6f);
	FSWGTerrainLayer& PaintLayer = AddLayer(ShaderData);
	FSWGTerrainBoundary& Circle = PaintLayer.Boundaries.AddDefaulted_GetRef();
	Circle.bEnabled = true;
	Circle.Type = ESWGTerrainBoundaryType::Circle;
	Circle.Radius = 10.0f;
	Circle.FeatheringAmount = 1.0f;
	FSWGTerrainAffector& Paint = PaintLayer.Affectors.AddDefaulted_GetRef();
	Paint.bEnabled = true;
	Paint.Type = ESWGTerrainAffectorType::ShaderConstant;
	Paint.ShaderFamilyId = 5;

	auto Weights = [&ShaderData](float X)
	{
		TMap<int32, float> Result;
		FSWGTerrainEvaluator::GetShaderWeights(ShaderData, X, 0.0f, Result);
		return Result;
	};
	TestEqual(TEXT("amount 0.75 reaches the family threshold 0.6"), Weights(5.0f).FindRef(5), 1.0f);
	TestEqual(TEXT("amount 0.36 stays under it"), Weights(8.0f).Num(), 0);
	Paint.bShaderThresholdOverride = true;
	Paint.ShaderThresholdOverride = 0.2f;
	TestEqual(TEXT("override threshold 0.2 paints at amount 0.36"), Weights(8.0f).FindRef(5), 1.0f);

	FSWGTerrainLayer& ReplaceLayer = AddLayer(ShaderData);
	FSWGTerrainAffector& Replace = ReplaceLayer.Affectors.AddDefaulted_GetRef();
	Replace.bEnabled = true;
	Replace.Type = ESWGTerrainAffectorType::ShaderReplace;
	Replace.ShaderOldFamilyId = 5;
	Replace.ShaderNewFamilyId = 6;
	const TMap<int32, float> Replaced = Weights(5.0f);
	TestTrue(TEXT("replace swaps the painted family outright"), Replaced.Num() == 1 && Replaced.FindRef(6) == 1.0f);
	Replace.ShaderOldFamilyId = 9;
	TestEqual(TEXT("replace leaves other families alone"), Weights(5.0f).FindRef(5), 1.0f);
	return true;
}
#endif
