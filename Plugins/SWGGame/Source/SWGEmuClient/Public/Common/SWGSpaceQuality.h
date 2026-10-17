#pragma once

#include "CoreMinimal.h"

/** What one swg.Space.Quality tier sets on the per-feature cvars. */
struct FSWGSpaceQualityTier
{
	int32 NebulaQuality;
	bool bNebulaWisps;
	float DustCountScale;
	bool bDebris;
	int32 RegionResolution;
	bool bHudFlat;
};

/**
 * swg.Space.Quality (0-3, default 3 = 4K on a high-end GPU) drives the per-feature cvars at ECVF_SetByScalability, so a
 * feature cvar set from code, the console or an ini keeps its own value.
 */
class SWGEMUCLIENT_API FSWGSpaceQuality
{
public:
	static constexpr int32 TierCount = 4;
	static constexpr int32 MaxTier = TierCount - 1;

	/** The table row for a tier; out-of-range tiers clamp. */
	static const FSWGSpaceQualityTier& GetTier(int32 Quality);
	/** swg.Space.Quality, clamped. */
	static int32 GetQuality();
	/** Pushes the current tier to the feature cvars that exist and have not been set above scalability priority. */
	static void Apply();
	/** Dust instances kept of the retail count, at least 1 when any is wanted. */
	static int32 ScaleDustCount(int32 RetailCount);
	static float GetDustCountScale();
	static int32 GetRegionResolution();
};
