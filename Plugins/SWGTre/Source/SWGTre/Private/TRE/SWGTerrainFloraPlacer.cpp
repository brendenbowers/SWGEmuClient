#include "TRE/SWGTerrainFloraPlacer.h"

#include "TRE/SWGTerrainNoise.h"

float FSWGTerrainFloraPlacer::GetEffectiveTileSize(const FSWGTerrainData& Data, ESWGTerrainFloraTier Tier)
{
	// Retail's collidable path (0xb5e780) hard-codes a 16 m grid; the header tile size is unused.
	return Tier == ESWGTerrainFloraTier::Collidable ? RetailCollidableTileSize : FMath::Max(Data.GetVegetationTier(Tier).TileSize, 0.5f);
}

float FSWGTerrainFloraPlacer::GetCellSize(const FSWGTerrainData& Data, ESWGTerrainFloraTier Tier)
{
	return GetEffectiveTileSize(Data, Tier) * SlotsPerCellAxis;
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
	const bool bRetailCollidable = Tier == ESWGTerrainFloraTier::Collidable;
	const float TileSize = GetEffectiveTileSize(Data, Tier);
	const float Border = TierParams.TileBorder >= 0.0f && TierParams.TileBorder * 2.0f < TileSize ? TierParams.TileBorder : 0.0f;
	const int32 RetailRowWidth = FMath::TruncToInt(Data.Header.MapSize * 0.0625f);
	const float Span = TileSize - 2.0f * Border;
	const bool bRadial = SWGIsRadialFloraTier(Tier);
	const float HalfMap = Data.Header.MapSize * 0.5f;

	const FIntPoint FirstTile = CellCoord * SlotsPerCellAxis;
	for (int32 TileOffsetY = 0; TileOffsetY < SlotsPerCellAxis; ++TileOffsetY)
	{
		for (int32 TileOffsetX = 0; TileOffsetX < SlotsPerCellAxis; ++TileOffsetX)
		{
			const FIntPoint TileCoord = FirstTile + FIntPoint(TileOffsetX, TileOffsetY);

			// Per-tile hashed stream. Retail's presence/child rolls come from a session-random chunk stream (time-seeded), so no seed can match them.
			FSWGTerrainRandom Random;
			Random.SetSeed((int32)(HashCombine(HashCombine(TierParams.Seed, (uint32)TileCoord.X * 73856093u), (uint32)TileCoord.Y * 19349663u) % 0x7fffffffu));
			auto UnitRoll = [](FSWGTerrainRandom& Stream) { return (float)Stream.Next() * 4.656613e-10f; };
			float OffsetX, OffsetY, YawRoll, ScaleRoll;
			if (bRetailCollidable)
			{
				// Retail: ran1 seeded with -(cellX + W/2 + (cellZ + W/2) * W), W = trunc(mapSize / 16); draws are jitter X, jitter Z, yaw, scale (0xb5e780).
				const int32 HalfRow = RetailRowWidth / 2;
				FSWGTerrainRandom CellRandom;
				CellRandom.SetSeed((TileCoord.X + HalfRow) + (TileCoord.Y + HalfRow) * RetailRowWidth);
				OffsetX = Border + UnitRoll(CellRandom) * Span;
				OffsetY = Border + UnitRoll(CellRandom) * Span;
				YawRoll = UnitRoll(CellRandom);
				ScaleRoll = UnitRoll(CellRandom);
			}
			else
			{
				OffsetX = Border + UnitRoll(Random) * Span;
				OffsetY = Border + UnitRoll(Random) * Span;
				YawRoll = 0.0f;
				ScaleRoll = 0.0f;
			}
			const float DensityRoll = UnitRoll(Random);
			const float ChildRoll = UnitRoll(Random);
			if (!bRetailCollidable)
			{
				YawRoll = UnitRoll(Random);
				ScaleRoll = UnitRoll(Random);
			}

			const FVector2D RawXY(TileCoord.X * TileSize + OffsetX, TileCoord.Y * TileSize + OffsetY);
			if (FMath::Abs(RawXY.X) > HalfMap || FMath::Abs(RawXY.Y) > HalfMap || IsBlocked(RawXY))
			{
				continue;
			}

			const FSWGTerrainFloraSample Sample = FSWGTerrainEvaluator::GetFlora(Data, RawXY.X, RawXY.Y, Tier, ExtraLayers);
			if (Sample.FamilyId == 0 || Sample.Density * DensityScale <= 0.0f || DensityRoll > Sample.Density * DensityScale)
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
				float WaterHeight;
				if (Family->bFloatsOnWater && FSWGTerrainEvaluator::GetWaterHeight(Data, RawXY.X, RawXY.Y, WaterHeight) && WaterHeight > Sample.Height)
				{
					Instance.RawPosition.Z = WaterHeight;
					bAlignToTerrain = false;
				}
			}

			if (bAlignToTerrain)
			{
				Instance.RawNormal = SampleRawNormal(Data, ExtraLayers, RawXY, Sample.Height);
			}

			OutInstances.Add(Instance);
		}
	}
}
