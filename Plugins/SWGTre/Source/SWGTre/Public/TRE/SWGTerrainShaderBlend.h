#pragma once

#include "CoreMinimal.h"

/**
 * Retail's per-tile shader-family overlay (tile builder 0x8612b0, see
 * Docs/RetailTerrainNotes.md "Shader-family blend overlay"). The map holds one family per
 * tile; where a neighbour tile carries a family with a higher group index, that family is
 * drawn over the tile through a 1/4, 1/2 or 3/4 alpha gradient (blnd_*.dds), rotated to face
 * the neighbour.
 */

/** Which blnd_*.dds a neighbour configuration uses: onequarter, onehalf, threequarter. */
enum class ESWGShaderOverlayShape : uint8
{
	Corner,
	Edge,
	ThreeQuarter,
};

/** One tile of the family map: the painted family id and its position in the planet's SGRP family list. */
struct FSWGShaderTileCell
{
	int32 Family = 0;
	int32 GroupIndex = 0;
};

struct FSWGShaderOverlay
{
	int32 Family = 0;
	int32 GroupIndex = 0;
	ESWGShaderOverlayShape Shape = ESWGShaderOverlayShape::Edge;
	/** Quarter turns, counter-clockwise on the map: 0 faces west, 1 south, 2 east, 3 north (corners: NW, SW, SE, NE). */
	int32 Rotation = 0;
};

/** The three alpha gradients, decoded once from the retail DXT3 textures. */
class SWGTRE_API FSWGShaderBlendMasks
{
public:
	/** Decodes mip 0 alpha of a DXT3 .dds into Size x Size values in 0..1; false when the file is not a square DXT3. */
	static bool DecodeDxt3Alpha(const TArray<uint8>& DdsBytes, int32& OutSize, TArray<float>& OutAlpha);

	/** Installs an already-decoded gradient (row 0 = north edge, column 0 = west edge in the rotation-0 frame). */
	void SetAlpha(ESWGShaderOverlayShape Shape, int32 Size, TArray<float> Alpha);

	bool LoadFromDds(ESWGShaderOverlayShape Shape, const TArray<uint8>& DdsBytes);

	bool IsReady() const;

	/**
	 * Overlay alpha at a point of the tile. East and South are 0..1 from the tile's west and north edges;
	 * Rotation turns the rotation-0 gradient counter-clockwise on the map in quarter turns.
	 */
	float Sample(ESWGShaderOverlayShape Shape, int32 Rotation, float East, float South) const;

private:
	int32 Sizes[3] = { 0, 0, 0 };
	TArray<float> Alpha[3];
};

class SWGTRE_API FSWGTerrainShaderBlend
{
public:
	static constexpr int32 CellCount = 9;
	static constexpr int32 CentreCell = 4;

	/** Index into the 3x3 cell array; offsets are -1..1 with +Y towards north (raw +Y). */
	static int32 CellIndex(int32 OffsetX, int32 OffsetY) { return (OffsetY + 1) * 3 + (OffsetX + 1); }

	/**
	 * The overlay layers retail builds for the centre cell: every distinct neighbour group index above the
	 * centre's, ascending (drawn in that order), each with the shape and rotation the neighbour pattern picks
	 * (0x8612b0: full-edge pairs first, then single edges W/N/E/S, then corners NW/NE/SE/SW).
	 */
	static void SelectOverlays(TConstArrayView<FSWGShaderTileCell> Cells, TArray<FSWGShaderOverlay>& OutOverlays);

	/** Final per-family weights at a point of the centre tile: the base family, then each overlay alpha-blended over it. */
	static void ComposeWeights(TConstArrayView<FSWGShaderTileCell> Cells, const FSWGShaderBlendMasks& Masks, float East, float South, TMap<int32, float>& OutWeights);

	/**
	 * World coordinate (along one axis) retail's family map is sampled at for a tile. The 4-pixel lattice
	 * pass (0xb65af0) makes two adjacent tiles share one sample, taken a half tile past the lattice line.
	 */
	static float GetTileSampleCoordinate(int32 Tile, float TileWidth);
};
