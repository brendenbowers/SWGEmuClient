#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGTerrainEvaluator.h"

/** One placed plant/rock/billboard. Raw space (x east, y north, z up, metres) — see Common/SWGWorldScale.h. */
struct FSWGTerrainFloraInstance
{
	FVector RawPosition = FVector::ZeroVector;

	/** Ground normal, or straight up when the child doesn't align to terrain. */
	FVector RawNormal = FVector::UpVector;

	/** Counter-clockwise in the raw x/y plane. */
	float YawRadians = 0.0f;

	/** Uniform scale for mesh flora; the billboard's width in metres for radial flora. */
	float Scale = 1.0f;

	int32 FamilyId = 0;

	/** Index into the family's Children (FSWGFloraFamily or FSWGRadialFamily, by tier). */
	int32 ChildIndex = 0;
};

/** Deterministic vegetation on inset tile grids. Per-tile seeds approximate retail streaming. */
class SWGTRE_API FSWGTerrainFloraPlacer
{
public:
	static constexpr int32 SlotsPerCellAxis = 8;

	/** Fixed grid of retail's collidable (tree) path, independent of the header tile size. */
	static constexpr float RetailCollidableTileSize = 16.0f;

	/** Metres one slot spans: 16 for the collidable tier, the header tile size otherwise. */
	static float GetEffectiveTileSize(const FSWGTerrainData& Data, ESWGTerrainFloraTier Tier);

	/** Raw metres one cell spans for a tier — SlotsPerCellAxis times the tier's TileSize. */
	static float GetCellSize(const FSWGTerrainData& Data, ESWGTerrainFloraTier Tier);

	static FIntPoint CellCoordAt(float CellSize, const FVector2D& RawPosition);

	/** Raw-space centre of a cell. */
	static FVector2D CellCenter(float CellSize, const FIntPoint& CellCoord);

	/**
	 * Fills every slot of one cell. DensityScale multiplies every density
	 * (a global thinning knob). IsBlocked rejects a raw XY position — ground
	 * under a building's room, say — before anything is evaluated there.
	 * Children whose appearance is a particle effect (.prt) still take their
	 * slot, preserving their weight in family selection.
	 */
	static void PlaceCell(const FSWGTerrainData& Data, TArrayView<const FSWGTerrainLayer> ExtraLayers, ESWGTerrainFloraTier Tier, const FIntPoint& CellCoord, float DensityScale, TFunctionRef<bool(const FVector2D&)> IsBlocked, TArray<FSWGTerrainFloraInstance>& OutInstances);

private:
	/** Ground normal from the height at two nearby offsets — only paid for children that align to terrain. */
	static FVector SampleRawNormal(const FSWGTerrainData& Data, TArrayView<const FSWGTerrainLayer> ExtraLayers, const FVector2D& RawPosition, float HeightAtPosition);
};
