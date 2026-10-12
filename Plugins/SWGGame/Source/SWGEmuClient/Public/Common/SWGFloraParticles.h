#pragma once

#include "CoreMinimal.h"

/**
 * Flora particle children (butterflies, beetles, fish) take a placement slot like any plant, so a tile can hold
 * hundreds. Each one is a whole effect, so a cell keeps only a thinned, spread-out subset of them.
 */
namespace SWGFloraParticles
{
	struct FPlacement
	{
		FString PrtPath;
		/** Raw space (x east, y north, z up, metres). */
		FVector RawPosition = FVector::ZeroVector;
		float YawRadians = 0.f;
	};

	struct FSampleSettings
	{
		/** Share of candidates eligible, 0..1. */
		float Fraction = 1.f;
		/** Kept placements are at least this far apart in the ground plane. */
		float MinSpacingMeters = 20.f;
		int32 MaxPerCell = 6;
	};

	/** Stable per-position rank in [0,1): the same plant ranks the same whenever its cell reloads. */
	inline float PositionRank(const FVector& RawPosition)
	{
		uint32 Hash = HashCombineFast(GetTypeHash(FMath::RoundToInt(RawPosition.X * 16.f)), GetTypeHash(FMath::RoundToInt(RawPosition.Y * 16.f)));
		// Finalizer so neighbouring grid points do not rank alike.
		Hash = (Hash ^ (Hash >> 16)) * 0x85ebca6bu;
		Hash = (Hash ^ (Hash >> 13)) * 0xc2b2ae35u;
		Hash ^= Hash >> 16;
		return (Hash >> 8) / float(0x1000000);
	}

	/** Indices of the candidates to keep: lowest rank first (spatially uniform, unlike slot order), rank < Fraction, spaced, capped. */
	inline TArray<int32> Sample(const TArray<FPlacement>& Candidates, const FSampleSettings& Settings)
	{
		struct FRanked
		{
			float Rank;
			int32 Index;
		};
		TArray<FRanked> Ranked;
		Ranked.Reserve(Candidates.Num());
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const float Rank = PositionRank(Candidates[Index].RawPosition);
			if (Rank < Settings.Fraction)
			{
				Ranked.Add({ Rank, Index });
			}
		}
		Ranked.Sort([](const FRanked& Left, const FRanked& Right) { return Left.Rank < Right.Rank; });

		const double MinSpacingSquared = FMath::Square((double)Settings.MinSpacingMeters);
		TArray<int32> Kept;
		for (const FRanked& Candidate : Ranked)
		{
			if (Kept.Num() >= Settings.MaxPerCell)
			{
				break;
			}
			const FVector& Position = Candidates[Candidate.Index].RawPosition;
			const bool bCrowded = Kept.ContainsByPredicate([&](int32 KeptIndex)
			{
				return FVector2D::DistSquared(FVector2D(Position), FVector2D(Candidates[KeptIndex].RawPosition)) < MinSpacingSquared;
			});
			if (!bCrowded)
			{
				Kept.Add(Candidate.Index);
			}
		}
		return Kept;
	}
}
