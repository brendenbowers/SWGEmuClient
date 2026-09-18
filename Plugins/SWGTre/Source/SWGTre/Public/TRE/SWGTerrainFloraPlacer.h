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

/**
 * Deterministic vegetation placement, reproducing what the retail client
 * streamed from FSWGTerrainHeader's tiers: each tier's ground is a grid of
 * TileSize-metre tiles, every tile holds at most one plant, jittered up to
 * TileBorder from the tile's centre, gated by the family/affector density at
 * that point and drawn from a generator seeded by the tile coordinate. A cell
 * is SlotsPerCellAxis tiles square — the unit the streaming layer loads and
 * unloads — so a cell's contents never depend on which cells around it are
 * loaded. Pure functions over immutable terrain data; safe on any thread.
 */
class SWGTRE_API FSWGTerrainFloraPlacer
{
public:
	static constexpr int32 SlotsPerCellAxis = 8;

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
	 * slot, keeping the distribution of the remaining children retail's.
	 */
	static void PlaceCell(const FSWGTerrainData& Data, TArrayView<const FSWGTerrainLayer> ExtraLayers, ESWGTerrainFloraTier Tier, const FIntPoint& CellCoord, float DensityScale, TFunctionRef<bool(const FVector2D&)> IsBlocked, TArray<FSWGTerrainFloraInstance>& OutInstances);

private:
	/** Ground normal from the height at two nearby offsets — only paid for children that align to terrain. */
	static FVector SampleRawNormal(const FSWGTerrainData& Data, TArrayView<const FSWGTerrainLayer> ExtraLayers, const FVector2D& RawPosition, float HeightAtPosition);
};
