#include "TRE/SWGTerrainFloraPlacer.h"

#include "Math/RandomStream.h"

float FSWGTerrainFloraPlacer::GetCellSize(const FSWGTerrainData& Data, ESWGTerrainFloraTier Tier)
{
	const float TileSize = Data.GetVegetationTier(Tier).TileSize;
	return FMath::Max(TileSize, 0.5f) * SlotsPerCellAxis;
}

FIntPoint FSWGTerrainFloraPlacer::CellCoordAt(float CellSize, const FVector2D& RawPosition)
{
	return FIntPoint(FMath::FloorToInt(RawPosition.X / CellSize), FMath::FloorToInt(RawPosition.Y / CellSize));
}

FVector2D FSWGTerrainFloraPlacer::CellCenter(float CellSize, const FIntPoint& CellCoord)
{
	return FVector2D((CellCoord.X + 0.5f) * CellSize, (CellCoord.Y + 0.5f) * CellSize);
}

FVector FSWGTerrainFloraPlacer::SampleRawNormal(const FSWGTerrainData& Data, TArrayView<const FSWGTerrainLayer> ExtraLayers, const FVector2D& RawPosition, float HeightAtPosition)
{
	constexpr float Step = 0.5f;
	const float HeightEast = FSWGTerrainEvaluator::GetHeight(Data, RawPosition.X + Step, RawPosition.Y, ExtraLayers);
	const float HeightNorth = FSWGTerrainEvaluator::GetHeight(Data, RawPosition.X, RawPosition.Y + Step, ExtraLayers);
	const FVector East(Step, 0.0f, HeightEast - HeightAtPosition);
	const FVector North(0.0f, Step, HeightNorth - HeightAtPosition);
	const FVector Normal = FVector::CrossProduct(East, North).GetSafeNormal();
	return Normal.Z < 0.0f ? -Normal : Normal;
}

void FSWGTerrainFloraPlacer::PlaceCell(const FSWGTerrainData& Data, TArrayView<const FSWGTerrainLayer> ExtraLayers, ESWGTerrainFloraTier Tier, const FIntPoint& CellCoord, float DensityScale, TFunctionRef<bool(const FVector2D&)> IsBlocked, TArray<FSWGTerrainFloraInstance>& OutInstances)
{
	const FSWGTerrainHeader::FVegetationTier& TierParams = Data.GetVegetationTier(Tier);
	const float TileSize = FMath::Max(TierParams.TileSize, 0.5f);
	const float Jitter = FMath::Clamp(TierParams.TileBorder, 0.0f, TileSize * 0.5f);
	const bool bRadial = SWGIsRadialFloraTier(Tier);
	const float HalfMap = Data.Header.MapSize * 0.5f;

	const FIntPoint FirstTile = CellCoord * SlotsPerCellAxis;
	for (int32 TileOffsetY = 0; TileOffsetY < SlotsPerCellAxis; ++TileOffsetY)
	{
		for (int32 TileOffsetX = 0; TileOffsetX < SlotsPerCellAxis; ++TileOffsetX)
		{
			const FIntPoint TileCoord = FirstTile + FIntPoint(TileOffsetX, TileOffsetY);

			// Every roll is drawn whether or not the slot fills, so the outcome
			// of one tile never shifts with its neighbours' — the stream is
			// seeded by the tile alone and the tier's own seed.
			FRandomStream Random((int32)HashCombine(HashCombine(TierParams.Seed, (uint32)TileCoord.X * 73856093u), (uint32)TileCoord.Y * 19349663u));
			const float OffsetX = Random.FRandRange(-Jitter, Jitter);
			const float OffsetY = Random.FRandRange(-Jitter, Jitter);
			const float DensityRoll = Random.FRand();
			const float ChildRoll = Random.FRand();
			const float YawRoll = Random.FRand();
			const float ScaleRoll = Random.FRand();

			const FVector2D RawXY((TileCoord.X + 0.5f) * TileSize + OffsetX, (TileCoord.Y + 0.5f) * TileSize + OffsetY);
			if (FMath::Abs(RawXY.X) > HalfMap || FMath::Abs(RawXY.Y) > HalfMap || IsBlocked(RawXY))
			{
				continue;
			}

			const FSWGTerrainFloraSample Sample = FSWGTerrainEvaluator::GetFlora(Data, RawXY.X, RawXY.Y, Tier, ExtraLayers);
			if (Sample.FamilyId == 0 || DensityRoll >= Sample.Density * DensityScale)
			{
				continue;
			}

			FSWGTerrainFloraInstance Instance;
			Instance.FamilyId = Sample.FamilyId;
			Instance.RawPosition = FVector(RawXY.X, RawXY.Y, Sample.Height);
			Instance.YawRadians = YawRoll * UE_TWO_PI;

			bool bAlignToTerrain = false;
			if (bRadial)
			{
				const FSWGRadialFamily* Family = Data.FindRadialFamily(Sample.FamilyId);
				const FSWGRadialChild* Child = Family ? Family->PickChild(ChildRoll) : nullptr;
				if (!Child) continue;
				Instance.ChildIndex = (int32)(Child - Family->Children.GetData());
				Instance.Scale = FMath::Lerp(Child->MinWidth, Child->MaxWidth, ScaleRoll);
				bAlignToTerrain = Child->bAlignToTerrain;
			}
			else
			{
				const FSWGFloraFamily* Family = Data.FindFloraFamily(Sample.FamilyId);
				const FSWGFloraChild* Child = Family ? Family->PickChild(ChildRoll) : nullptr;
				if (!Child) continue;
				Instance.ChildIndex = (int32)(Child - Family->Children.GetData());
				Instance.Scale = Child->bShouldScale ? FMath::Lerp(Child->MinScale, Child->MaxScale, ScaleRoll) : 1.0f;
				bAlignToTerrain = Child->bAlignToTerrain;
			}

			if (bAlignToTerrain)
			{
				Instance.RawNormal = SampleRawNormal(Data, ExtraLayers, RawXY, Sample.Height);
			}

			OutInstances.Add(Instance);
		}
	}
}
