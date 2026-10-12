#pragma once

#include "CoreMinimal.h"

/** Wind -> cloth clip selection for flags and banners (the loop_stand clips of flag.lat / banner.lat). */
namespace SWGPropWind
{
	/** Wind used when nothing (no weather message, no console override) has set one: medium. */
	constexpr float DefaultWindStrength = 0.5f;

	/** Metres per second that a wind strength of 1 stands for; the cdf's MASP (10 in every file) is the same unit. */
	constexpr float ReferenceMaxWindMetersPerSecond = 10.0f;

	/** Clip intensity tiers, calmest first. */
	enum class ETier : uint8
	{
		Idle,
		Light,
		Medium,
		Heavy,
		Unknown,
	};

	/** The tier a clip's file name announces ("flag_heavy_wind_repeat", "banner_light"); Unknown when it names none. */
	inline ETier ClassifyClip(const FString& ClipPath)
	{
		const FString Name = FPaths::GetBaseFilename(ClipPath).ToLower();
		if (Name.Contains(TEXT("heavy"))) { return ETier::Heavy; }
		if (Name.Contains(TEXT("medium"))) { return ETier::Medium; }
		if (Name.Contains(TEXT("light"))) { return ETier::Light; }
		if (Name.Contains(TEXT("idle"))) { return ETier::Idle; }
		return ETier::Unknown;
	}

	/**
	 * Wind strength a cdf flag feels: the global strength scaled by WSCL and capped by MASP (m/s). Every retail
	 * flag has WSCL 1 and MASP 10, so this is the identity until a weather row supplies real speeds.
	 */
	inline float ResolveFlagWind(float WindStrength, float WindScale, float MaxWindSpeed)
	{
		const float MetersPerSecond = FMath::Clamp(WindStrength, 0.f, 1.f) * ReferenceMaxWindMetersPerSecond * WindScale;
		return MaxWindSpeed > KINDA_SMALL_NUMBER ? FMath::Clamp(MetersPerSecond / MaxWindSpeed, 0.f, 1.f) : 0.f;
	}

	/**
	 * Index into ClipPaths of the clip for a wind strength 0..1. The classified clips are ordered calm to heavy and the
	 * range is split evenly across them (3 tiers: idle < 1/3 <= medium < 2/3 <= heavy; 4 adds light), so 0.5 lands on
	 * medium for both flags and banners. Unclassified clips are ignored; -1 when none is classified.
	 */
	inline int32 SelectClipIndex(const TArray<FString>& ClipPaths, float WindStrength)
	{
		TArray<TPair<ETier, int32>> Ranked;
		for (int32 ClipIndex = 0; ClipIndex < ClipPaths.Num(); ++ClipIndex)
		{
			const ETier Tier = ClassifyClip(ClipPaths[ClipIndex]);
			if (Tier != ETier::Unknown)
			{
				Ranked.Emplace(Tier, ClipIndex);
			}
		}
		if (Ranked.IsEmpty())
		{
			return -1;
		}

		Ranked.Sort([](const TPair<ETier, int32>& Left, const TPair<ETier, int32>& Right) { return Left.Key < Right.Key; });
		const int32 TierCount = Ranked.Num();
		const int32 Pick = FMath::Clamp(FMath::FloorToInt(FMath::Clamp(WindStrength, 0.f, 1.f) * TierCount), 0, TierCount - 1);
		return Ranked[Pick].Value;
	}
}
