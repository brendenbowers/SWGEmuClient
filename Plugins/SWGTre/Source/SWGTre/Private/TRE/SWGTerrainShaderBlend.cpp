#include "TRE/SWGTerrainShaderBlend.h"

namespace
{
	int32 ShapeIndex(ESWGShaderOverlayShape Shape) { return static_cast<int32>(Shape); }

	int32 FloorDivide(int32 Value, int32 Divisor)
	{
		int32 Quotient = Value / Divisor;
		if ((Value % Divisor != 0) && ((Value < 0) != (Divisor < 0)))
		{
			--Quotient;
		}
		return Quotient;
	}
}

bool FSWGShaderBlendMasks::DecodeDxt3Alpha(const TArray<uint8>& DdsBytes, int32& OutSize, TArray<float>& OutAlpha)
{
	constexpr int32 HeaderSize = 128;
	if (DdsBytes.Num() < HeaderSize || FMemory::Memcmp(DdsBytes.GetData(), "DDS ", 4) != 0 || FMemory::Memcmp(DdsBytes.GetData() + 84, "DXT3", 4) != 0)
	{
		return false;
	}

	uint32 Height = 0;
	uint32 Width = 0;
	FMemory::Memcpy(&Height, DdsBytes.GetData() + 12, 4);
	FMemory::Memcpy(&Width, DdsBytes.GetData() + 16, 4);
	if (Width != Height || Width < 4 || (Width % 4) != 0)
	{
		return false;
	}

	const int32 Size = static_cast<int32>(Width);
	const int32 BlocksPerRow = Size / 4;
	if (DdsBytes.Num() < HeaderSize + BlocksPerRow * BlocksPerRow * 16)
	{
		return false;
	}

	OutSize = Size;
	OutAlpha.SetNumUninitialized(Size * Size);
	for (int32 BlockRow = 0; BlockRow < BlocksPerRow; ++BlockRow)
	{
		for (int32 BlockColumn = 0; BlockColumn < BlocksPerRow; ++BlockColumn)
		{
			// DXT3: 8 bytes of 4-bit alpha (a row per uint16, leftmost pixel in the low nibble), then 8 bytes of colour.
			const uint8* Block = DdsBytes.GetData() + HeaderSize + (BlockRow * BlocksPerRow + BlockColumn) * 16;
			for (int32 PixelRow = 0; PixelRow < 4; ++PixelRow)
			{
				const uint32 RowBits = Block[PixelRow * 2] | (static_cast<uint32>(Block[PixelRow * 2 + 1]) << 8);
				for (int32 PixelColumn = 0; PixelColumn < 4; ++PixelColumn)
				{
					const uint32 Nibble = (RowBits >> (4 * PixelColumn)) & 0xF;
					OutAlpha[(BlockRow * 4 + PixelRow) * Size + BlockColumn * 4 + PixelColumn] = Nibble / 15.0f;
				}
			}
		}
	}
	return true;
}

void FSWGShaderBlendMasks::SetAlpha(ESWGShaderOverlayShape Shape, int32 Size, TArray<float> NewAlpha)
{
	Sizes[ShapeIndex(Shape)] = Size;
	Alpha[ShapeIndex(Shape)] = MoveTemp(NewAlpha);
}

bool FSWGShaderBlendMasks::LoadFromDds(ESWGShaderOverlayShape Shape, const TArray<uint8>& DdsBytes)
{
	int32 Size = 0;
	TArray<float> Decoded;
	if (!DecodeDxt3Alpha(DdsBytes, Size, Decoded))
	{
		return false;
	}
	SetAlpha(Shape, Size, MoveTemp(Decoded));
	return true;
}

bool FSWGShaderBlendMasks::IsReady() const
{
	return Sizes[0] > 0 && Sizes[1] > 0 && Sizes[2] > 0;
}

float FSWGShaderBlendMasks::Sample(ESWGShaderOverlayShape Shape, int32 Rotation, float East, float South) const
{
	const int32 Size = Sizes[ShapeIndex(Shape)];
	if (Size <= 0)
	{
		return 0.0f;
	}

	// Undo the quarter turns on the point instead of rotating the image: a counter-clockwise turn of the
	// pattern (west -> south -> east -> north) is a clockwise turn of the sample point, (x, y) -> (-y, x) in
	// east/south axes.
	float OffsetEast = East - 0.5f;
	float OffsetSouth = South - 0.5f;
	for (int32 Turn = 0; Turn < (Rotation & 3); ++Turn)
	{
		const float PreviousEast = OffsetEast;
		OffsetEast = -OffsetSouth;
		OffsetSouth = PreviousEast;
	}

	const float TexelX = FMath::Clamp((OffsetEast + 0.5f) * Size - 0.5f, 0.0f, Size - 1.0f);
	const float TexelY = FMath::Clamp((OffsetSouth + 0.5f) * Size - 0.5f, 0.0f, Size - 1.0f);
	const int32 Left = FMath::FloorToInt(TexelX);
	const int32 Top = FMath::FloorToInt(TexelY);
	const int32 Right = FMath::Min(Left + 1, Size - 1);
	const int32 Bottom = FMath::Min(Top + 1, Size - 1);
	const float FractionX = TexelX - Left;
	const float FractionY = TexelY - Top;

	const TArray<float>& Texels = Alpha[ShapeIndex(Shape)];
	const float TopValue = FMath::Lerp(Texels[Top * Size + Left], Texels[Top * Size + Right], FractionX);
	const float BottomValue = FMath::Lerp(Texels[Bottom * Size + Left], Texels[Bottom * Size + Right], FractionX);
	return FMath::Lerp(TopValue, BottomValue, FractionY);
}

void FSWGTerrainShaderBlend::SelectOverlays(TConstArrayView<FSWGShaderTileCell> Cells, TArray<FSWGShaderOverlay>& OutOverlays)
{
	OutOverlays.Reset();
	if (Cells.Num() != CellCount)
	{
		return;
	}

	// Distinct group indices above the centre's, ascending (retail's insertion sort at 0x861441).
	const int32 CentreIndex = Cells[CentreCell].GroupIndex;
	TArray<FSWGShaderTileCell, TInlineAllocator<CellCount>> Ranked;
	for (const FSWGShaderTileCell& Cell : Cells)
	{
		if (Cell.GroupIndex <= CentreIndex || Ranked.ContainsByPredicate([&Cell](const FSWGShaderTileCell& Other) { return Other.GroupIndex == Cell.GroupIndex; }))
		{
			continue;
		}
		int32 InsertAt = 0;
		while (InsertAt < Ranked.Num() && Ranked[InsertAt].GroupIndex < Cell.GroupIndex)
		{
			++InsertAt;
		}
		Ranked.Insert(Cell, InsertAt);
	}

	const int32 West = Cells[CellIndex(-1, 0)].Family;
	const int32 East = Cells[CellIndex(1, 0)].Family;
	const int32 North = Cells[CellIndex(0, 1)].Family;
	const int32 South = Cells[CellIndex(0, -1)].Family;
	const int32 NorthWest = Cells[CellIndex(-1, 1)].Family;
	const int32 NorthEast = Cells[CellIndex(1, 1)].Family;
	const int32 SouthWest = Cells[CellIndex(-1, -1)].Family;
	const int32 SouthEast = Cells[CellIndex(1, -1)].Family;

	for (const FSWGShaderTileCell& Layer : Ranked)
	{
		const int32 Family = Layer.Family;
		FSWGShaderOverlay Overlay;
		Overlay.Family = Family;
		Overlay.GroupIndex = Layer.GroupIndex;

		auto Choose = [&Overlay](ESWGShaderOverlayShape Shape, int32 Rotation)
		{
			Overlay.Shape = Shape;
			Overlay.Rotation = Rotation;
		};

		// The same priority order as the branch ladder at 0x8615c9..0x8617a7.
		if (West == Family && North == Family)
		{
			Choose(ESWGShaderOverlayShape::ThreeQuarter, 0);
		}
		else if (East == Family && North == Family)
		{
			Choose(ESWGShaderOverlayShape::ThreeQuarter, 3);
		}
		else if (East == Family && South == Family)
		{
			Choose(ESWGShaderOverlayShape::ThreeQuarter, 2);
		}
		else if (West == Family && South == Family)
		{
			Choose(ESWGShaderOverlayShape::ThreeQuarter, 1);
		}
		else if (West == Family)
		{
			Choose(ESWGShaderOverlayShape::Edge, 0);
		}
		else if (North == Family)
		{
			Choose(ESWGShaderOverlayShape::Edge, 3);
		}
		else if (East == Family)
		{
			Choose(ESWGShaderOverlayShape::Edge, 2);
		}
		else if (South == Family)
		{
			Choose(ESWGShaderOverlayShape::Edge, 1);
		}
		else if (NorthWest == Family)
		{
			Choose(ESWGShaderOverlayShape::Corner, 0);
		}
		else if (NorthEast == Family)
		{
			Choose(ESWGShaderOverlayShape::Corner, 3);
		}
		else if (SouthEast == Family)
		{
			Choose(ESWGShaderOverlayShape::Corner, 2);
		}
		else if (SouthWest == Family)
		{
			Choose(ESWGShaderOverlayShape::Corner, 1);
		}
		else
		{
			continue;
		}
		OutOverlays.Add(Overlay);
	}
}

void FSWGTerrainShaderBlend::ComposeWeights(TConstArrayView<FSWGShaderTileCell> Cells, const FSWGShaderBlendMasks& Masks, float East, float South, TMap<int32, float>& OutWeights)
{
	OutWeights.Reset();
	if (Cells.Num() != CellCount)
	{
		return;
	}

	// Family 0 is "nothing painted": no weight of its own, so its share falls to the material's implicit first layer.
	if (Cells[CentreCell].Family != 0)
	{
		OutWeights.Add(Cells[CentreCell].Family, 1.0f);
	}

	TArray<FSWGShaderOverlay> Overlays;
	SelectOverlays(Cells, Overlays);
	for (const FSWGShaderOverlay& Overlay : Overlays)
	{
		const float Alpha = FMath::Clamp(Masks.Sample(Overlay.Shape, Overlay.Rotation, East, South), 0.0f, 1.0f);
		for (TPair<int32, float>& Pair : OutWeights)
		{
			Pair.Value *= 1.0f - Alpha;
		}
		OutWeights.FindOrAdd(Overlay.Family) += Alpha;
	}
}

float FSWGTerrainShaderBlend::GetTileSampleCoordinate(int32 Tile, float TileWidth)
{
	const int32 Block = FloorDivide(Tile + 1, 2);
	return Block * 2.0f * TileWidth + TileWidth * 0.5f;
}
