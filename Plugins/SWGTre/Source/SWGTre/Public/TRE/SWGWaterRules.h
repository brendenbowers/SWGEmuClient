#pragma once

#include "CoreMinimal.h"

/** Retail water rules (SWGEmu.exe, see Docs/RetailTerrainNotes.md session 5). Heights are raw-space metres. */
namespace SWGWaterRules
{
	/** Underwater clear colour the retail frame uses when the camera is below the surface (0x51b770). */
	inline FColor GetUnderwaterColor() { return FColor(0x4d, 0x73, 0x71); }

	/** Far clip the retail camera is given while underwater, in metres (0x51b770). */
	constexpr float UnderwaterFarPlane = 500.0f;

	/** Terrain::isWater (0xb5c2f0): a point is in water when a surface exists and the point is strictly below it. */
	inline bool IsInWater(bool bHasWater, float WaterHeight, float PointHeight)
	{
		return bHasWater && PointHeight < WaterHeight;
	}

	/**
	 * CreatureController swim flag (0x63a150, stored at controller+0x55): the ground is more than SwimHeight below the
	 * surface. SwimHeight is the creature template's swimHeight times the object's scale.
	 */
	inline bool IsSwimming(bool bHasWater, float WaterHeight, float GroundHeight, float SwimHeight)
	{
		return bHasWater && GroundHeight < WaterHeight - SwimHeight;
	}

	/** Wading: feet in water (ground below the surface) without being deep enough to swim. */
	inline bool IsWading(bool bHasWater, float WaterHeight, float GroundHeight, float SwimHeight)
	{
		return bHasWater && GroundHeight < WaterHeight && !IsSwimming(bHasWater, WaterHeight, GroundHeight, SwimHeight);
	}

	/** Core3's PlayerManager swim test (the server's SWIMMING state): land + swimHeight - water < 0.2. Slightly more eager than the client flag. */
	inline bool ServerWouldSwim(bool bHasWater, float WaterHeight, float GroundHeight, float SwimHeight)
	{
		return bHasWater && WaterHeight > GroundHeight && GroundHeight + SwimHeight - WaterHeight < 0.2f;
	}

	/** Core3's findClosestWorldFloor clamp: a swimmer floats SwimHeight under the surface. */
	inline float GetSwimmerHeight(bool bHasWater, float WaterHeight, float GroundHeight, float SwimHeight)
	{
		return (bHasWater && WaterHeight > GroundHeight + SwimHeight) ? WaterHeight - SwimHeight : GroundHeight;
	}

	/** Water depth over the ground, 0 on dry land. */
	inline float GetDepth(bool bHasWater, float WaterHeight, float GroundHeight)
	{
		return bHasWater ? FMath::Max(0.0f, WaterHeight - GroundHeight) : 0.0f;
	}
}
