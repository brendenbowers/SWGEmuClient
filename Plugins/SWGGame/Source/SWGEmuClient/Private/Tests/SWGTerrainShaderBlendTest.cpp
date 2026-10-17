#include "Misc/AutomationTest.h"
#include "TRE/SWGTerrainShaderBlend.h"

#if WITH_DEV_AUTOMATION_TESTS
// Synthetic 3x3 family maps, no TRE: pins the neighbour configuration -> overlay shape/rotation ladder of
// the retail tile builder (0x8612b0) and the mask rotation convention.
namespace
{
	using FCells = TStaticArray<FSWGShaderTileCell, 9>;

	// Group index = family id, so a higher family id outranks a lower one.
	FCells MakeCells(int32 CentreFamily)
	{
		FCells Cells;
		for (FSWGShaderTileCell& Cell : Cells)
		{
			Cell.Family = CentreFamily;
			Cell.GroupIndex = CentreFamily;
		}
		return Cells;
	}

	void SetCell(FCells& Cells, int32 OffsetX, int32 OffsetY, int32 Family)
	{
		FSWGShaderTileCell& Cell = Cells[FSWGTerrainShaderBlend::CellIndex(OffsetX, OffsetY)];
		Cell.Family = Family;
		Cell.GroupIndex = Family;
	}

	TArray<FSWGShaderOverlay> Select(const FCells& Cells)
	{
		TArray<FSWGShaderOverlay> Overlays;
		FSWGTerrainShaderBlend::SelectOverlays(TConstArrayView<FSWGShaderTileCell>(Cells.GetData(), 9), Overlays);
		return Overlays;
	}

	// Rotation-0 gradients: edge fades west -> east, corner is a radial from the NW corner, three-quarter
	// is opaque on both the west and north sides.
	FSWGShaderBlendMasks MakeMasks()
	{
		constexpr int32 Size = 16;
		FSWGShaderBlendMasks Masks;
		TArray<float> Edge, Corner, ThreeQuarter;
		for (int32 Row = 0; Row < Size; ++Row)
		{
			for (int32 Column = 0; Column < Size; ++Column)
			{
				const float East = (Column + 0.5f) / Size;
				const float South = (Row + 0.5f) / Size;
				Edge.Add(1.0f - East);
				Corner.Add(FMath::Clamp(1.0f - FMath::Sqrt(East * East + South * South), 0.0f, 1.0f));
				ThreeQuarter.Add(FMath::Clamp(2.0f - East - South, 0.0f, 1.0f));
			}
		}
		Masks.SetAlpha(ESWGShaderOverlayShape::Edge, Size, MoveTemp(Edge));
		Masks.SetAlpha(ESWGShaderOverlayShape::Corner, Size, MoveTemp(Corner));
		Masks.SetAlpha(ESWGShaderOverlayShape::ThreeQuarter, Size, MoveTemp(ThreeQuarter));
		return Masks;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGTerrainShaderBlendTest, "SWGEmu.Tre.Terrain.ShaderBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGTerrainShaderBlendTest::RunTest(const FString& Parameters)
{
	// Uniform map: nothing to overlay.
	TestEqual(TEXT("uniform map has no overlays"), Select(MakeCells(3)).Num(), 0);

	// A neighbour with a lower group index never overlays the centre.
	{
		FCells Cells = MakeCells(5);
		SetCell(Cells, -1, 0, 2);
		TestEqual(TEXT("lower-ranked neighbour is ignored"), Select(Cells).Num(), 0);
	}

	struct FCase
	{
		const TCHAR* Name;
		TArray<FIntPoint> Offsets;
		ESWGShaderOverlayShape Shape;
		int32 Rotation;
	};
	const TArray<FCase> Cases = {
		{ TEXT("west edge"), { FIntPoint(-1, 0) }, ESWGShaderOverlayShape::Edge, 0 },
		{ TEXT("south edge"), { FIntPoint(0, -1) }, ESWGShaderOverlayShape::Edge, 1 },
		{ TEXT("east edge"), { FIntPoint(1, 0) }, ESWGShaderOverlayShape::Edge, 2 },
		{ TEXT("north edge"), { FIntPoint(0, 1) }, ESWGShaderOverlayShape::Edge, 3 },
		{ TEXT("north-west corner"), { FIntPoint(-1, 1) }, ESWGShaderOverlayShape::Corner, 0 },
		{ TEXT("south-west corner"), { FIntPoint(-1, -1) }, ESWGShaderOverlayShape::Corner, 1 },
		{ TEXT("south-east corner"), { FIntPoint(1, -1) }, ESWGShaderOverlayShape::Corner, 2 },
		{ TEXT("north-east corner"), { FIntPoint(1, 1) }, ESWGShaderOverlayShape::Corner, 3 },
		{ TEXT("west+north"), { FIntPoint(-1, 0), FIntPoint(0, 1) }, ESWGShaderOverlayShape::ThreeQuarter, 0 },
		{ TEXT("west+south"), { FIntPoint(-1, 0), FIntPoint(0, -1) }, ESWGShaderOverlayShape::ThreeQuarter, 1 },
		{ TEXT("east+south"), { FIntPoint(1, 0), FIntPoint(0, -1) }, ESWGShaderOverlayShape::ThreeQuarter, 2 },
		{ TEXT("east+north"), { FIntPoint(1, 0), FIntPoint(0, 1) }, ESWGShaderOverlayShape::ThreeQuarter, 3 },
		// Retail's ladder tests an edge pair before the lone edges, and any edge before a corner.
		{ TEXT("west+north+north-west"), { FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(-1, 1) }, ESWGShaderOverlayShape::ThreeQuarter, 0 },
		{ TEXT("west+east (no pair) takes west"), { FIntPoint(-1, 0), FIntPoint(1, 0) }, ESWGShaderOverlayShape::Edge, 0 },
		{ TEXT("south edge beats north-east corner"), { FIntPoint(0, -1), FIntPoint(1, 1) }, ESWGShaderOverlayShape::Edge, 1 },
	};
	for (const FCase& Case : Cases)
	{
		FCells Cells = MakeCells(1);
		for (const FIntPoint& Offset : Case.Offsets)
		{
			SetCell(Cells, Offset.X, Offset.Y, 4);
		}
		const TArray<FSWGShaderOverlay> Overlays = Select(Cells);
		if (TestEqual(*FString::Printf(TEXT("%s: one overlay"), Case.Name), Overlays.Num(), 1))
		{
			TestEqual(*FString::Printf(TEXT("%s: family"), Case.Name), Overlays[0].Family, 4);
			TestTrue(*FString::Printf(TEXT("%s: shape"), Case.Name), Overlays[0].Shape == Case.Shape);
			TestEqual(*FString::Printf(TEXT("%s: rotation"), Case.Name), Overlays[0].Rotation, Case.Rotation);
		}
	}

	// Several higher families stack in ascending group order, one layer per group index.
	{
		FCells Cells = MakeCells(1);
		SetCell(Cells, 1, 0, 7);
		SetCell(Cells, -1, 0, 3);
		SetCell(Cells, 0, 1, 7);
		const TArray<FSWGShaderOverlay> Overlays = Select(Cells);
		if (TestEqual(TEXT("two higher families give two layers"), Overlays.Num(), 2))
		{
			TestEqual(TEXT("lower group index is drawn first"), Overlays[0].Family, 3);
			TestEqual(TEXT("higher group index is drawn last"), Overlays[1].Family, 7);
			TestTrue(TEXT("east+north pair of the top family"), Overlays[1].Shape == ESWGShaderOverlayShape::ThreeQuarter && Overlays[1].Rotation == 3);
		}
	}

	// Mask rotation: an edge overlay is opaque on the side it faces and clear on the opposite side.
	const FSWGShaderBlendMasks Masks = MakeMasks();
	const FVector2f Sides[4] = { FVector2f(0.02f, 0.5f), FVector2f(0.5f, 0.98f), FVector2f(0.98f, 0.5f), FVector2f(0.5f, 0.02f) };
	for (int32 Rotation = 0; Rotation < 4; ++Rotation)
	{
		const FVector2f& Facing = Sides[Rotation];
		const FVector2f& Away = Sides[(Rotation + 2) & 3];
		TestTrue(*FString::Printf(TEXT("edge rotation %d is opaque on its side"), Rotation),
			Masks.Sample(ESWGShaderOverlayShape::Edge, Rotation, Facing.X, Facing.Y) > 0.9f);
		TestTrue(*FString::Printf(TEXT("edge rotation %d is clear opposite"), Rotation),
			Masks.Sample(ESWGShaderOverlayShape::Edge, Rotation, Away.X, Away.Y) < 0.1f);
	}
	const FVector2f Corners[4] = { FVector2f(0.02f, 0.02f), FVector2f(0.02f, 0.98f), FVector2f(0.98f, 0.98f), FVector2f(0.98f, 0.02f) };
	for (int32 Rotation = 0; Rotation < 4; ++Rotation)
	{
		TestTrue(*FString::Printf(TEXT("corner rotation %d is opaque in its corner"), Rotation),
			Masks.Sample(ESWGShaderOverlayShape::Corner, Rotation, Corners[Rotation].X, Corners[Rotation].Y) > 0.9f);
		const FVector2f& Opposite = Corners[(Rotation + 2) & 3];
		TestTrue(*FString::Printf(TEXT("corner rotation %d is clear in the opposite corner"), Rotation),
			Masks.Sample(ESWGShaderOverlayShape::Corner, Rotation, Opposite.X, Opposite.Y) < 0.1f);
	}

	// Composite: family weights always sum to one and the overlay takes the facing side.
	{
		FCells Cells = MakeCells(1);
		SetCell(Cells, -1, 0, 4);
		TMap<int32, float> Weights;
		FSWGTerrainShaderBlend::ComposeWeights(TConstArrayView<FSWGShaderTileCell>(Cells.GetData(), 9), Masks, 0.02f, 0.5f, Weights);
		TestTrue(TEXT("overlay dominates at the neighbour side"), Weights.FindRef(4) > 0.9f);
		FSWGTerrainShaderBlend::ComposeWeights(TConstArrayView<FSWGShaderTileCell>(Cells.GetData(), 9), Masks, 0.6f, 0.5f, Weights);
		TestEqual(TEXT("weights sum to one"), Weights.FindRef(1) + Weights.FindRef(4), 1.0f, 1e-4f);
		TestTrue(TEXT("base dominates away from the neighbour"), Weights.FindRef(1) > 0.5f);
	}

	// Lattice sample points (two tiles share one, half a tile past the lattice line).
	TestEqual(TEXT("tile 0 sample"), FSWGTerrainShaderBlend::GetTileSampleCoordinate(0, 16.0f), 8.0f);
	TestEqual(TEXT("tile -1 shares tile 0's sample"), FSWGTerrainShaderBlend::GetTileSampleCoordinate(-1, 16.0f), 8.0f);
	TestEqual(TEXT("tile 1 sample"), FSWGTerrainShaderBlend::GetTileSampleCoordinate(1, 16.0f), 40.0f);
	TestEqual(TEXT("tile 2 shares tile 1's sample"), FSWGTerrainShaderBlend::GetTileSampleCoordinate(2, 16.0f), 40.0f);
	TestEqual(TEXT("tile -2 sample"), FSWGTerrainShaderBlend::GetTileSampleCoordinate(-2, 16.0f), -24.0f);

	// DXT3 alpha decode: one 8x8 file, top-left block alpha nibbles 0..F across the first row.
	{
		TArray<uint8> Dds;
		Dds.SetNumZeroed(128 + 4 * 16);
		FMemory::Memcpy(Dds.GetData(), "DDS ", 4);
		const uint32 Dimension = 8;
		FMemory::Memcpy(Dds.GetData() + 12, &Dimension, 4);
		FMemory::Memcpy(Dds.GetData() + 16, &Dimension, 4);
		FMemory::Memcpy(Dds.GetData() + 84, "DXT3", 4);
		Dds[128] = 0x10;
		Dds[129] = 0x32;
		int32 Size = 0;
		TArray<float> Alpha;
		if (TestTrue(TEXT("dxt3 decodes"), FSWGShaderBlendMasks::DecodeDxt3Alpha(Dds, Size, Alpha)))
		{
			TestEqual(TEXT("dxt3 size"), Size, 8);
			TestEqual(TEXT("first pixel is the low nibble"), Alpha[0], 0.0f);
			TestEqual(TEXT("second pixel"), Alpha[1], 1.0f / 15.0f, 1e-5f);
			TestEqual(TEXT("fourth pixel is the high nibble of the second byte"), Alpha[3], 3.0f / 15.0f, 1e-5f);
		}
		Dds[84] = 'X';
		TestFalse(TEXT("non-DXT3 is rejected"), FSWGShaderBlendMasks::DecodeDxt3Alpha(Dds, Size, Alpha));
	}

	return true;
}
#endif
