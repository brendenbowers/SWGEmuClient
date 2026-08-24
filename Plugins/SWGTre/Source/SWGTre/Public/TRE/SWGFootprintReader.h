#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGTerrainReader.h"

/**
 * One cell of a structure footprint grid, as encoded by the single character
 * the PRNT chunk stores for it. The whole vocabulary really is these three —
 * surveyed across all 74 unique footprint/**\/*.sfp files in the retail TREs
 * (data_other_00.tre + patch_00.tre), whose combined character histogram is
 * exactly { 'F': 879, 'H': 895, '.': 50 }.
 */
enum class ESWGFootprintCell : uint8
{
	/** '.' — outside the footprint entirely. Neither reserved nor flattened. */
	Outside,
	/** 'H' — reserved clearance around the structure: blocks other placements, but does not affect terrain. */
	Reserved,
	/** 'F' — the structure body: terrain flattens to the structure's base height here, and nothing else may overlap. */
	Structure,
};

/**
 * A parsed .sfp (structure footprint), the retail mechanism by which a
 * *player-placed* structure flattens the terrain it sits on.
 *
 * IMPORTANT COVERAGE NOTE: footprints only exist for player-placeable content
 * — player houses, player-city buildings, harvesters/installations, faction
 * perks. Static world buildings from the snapshot .ws files have no footprint
 * at all (all 55 templates in Core3's object/building/tatooine/objects.lua
 * carry structureFootprintFileName = ""), because their pads are pre-baked
 * into the .trn as designer-authored boundary + AffectorHeightConstant layers.
 * So this reader is not the fix for a static building intersecting terrain.
 *
 * Wire format (confirmed by decoding real files; Core3's own StructureFootprint
 * parses INFO but leaves PRNT as a //TODO, so there was no reference to port):
 *
 *   FORM ▸ FOOT ▸ FORM 0000
 *     INFO (24 bytes, little-endian):
 *       int32 cols, int32 rows, int32 centerCol, int32 centerRow,
 *       float colChunkSize, float rowChunkSize     // metres per cell
 *     PRNT:
 *       one null-terminated ASCII string per row, one character per column
 *
 * e.g. footprint/building/player/shared_player_house_tatooine_large_style_01.sfp
 * is 7x5, center (3,2), 8.0 x 8.0 m cells:
 *
 *     HFFFFFH
 *     HFFFFFH
 *     HFFFFFH
 *     HFFFFFH
 *     HFFFFFH
 */
struct FSWGStructureFootprint
{
	int32 Cols = 0;
	int32 Rows = 0;

	/** Cell index the structure's own origin sits in. Not always the geometric middle — even-sized grids are off-center on purpose. */
	int32 CenterCol = 0;
	int32 CenterRow = 0;

	/** Metres covered by one cell. Observed values across retail data: 2, 3, 4, 6, 8, 16 and 32. */
	float ColChunkSize = 0.0f;
	float RowChunkSize = 0.0f;

	/** Row-major, Rows * Cols entries. */
	TArray<ESWGFootprintCell> Cells;

	bool IsValid() const { return Cols > 0 && Rows > 0 && Cells.Num() == Cols * Rows; }

	ESWGFootprintCell GetCell(int32 Col, int32 Row) const
	{
		if (Col < 0 || Col >= Cols || Row < 0 || Row >= Rows)
		{
			return ESWGFootprintCell::Outside;
		}
		return Cells[Row * Cols + Col];
	}

	/** Inclusive cell-index bounds of the 'F' region. False when the grid has no 'F' cell at all. */
	bool GetStructureCellBounds(FIntPoint& OutMin, FIntPoint& OutMax) const;

	/**
	 * Axis-aligned extent of the 'F' region in footprint-local metres, with the
	 * structure origin at the *centre of the centre cell* — so cell (Col,Row)
	 * spans ((Col - CenterCol) +/- 0.5) * ColChunkSize on X, likewise on Y.
	 * False when the grid has no 'F' cell.
	 */
	bool GetStructureLocalBounds(FBox2D& OutBounds) const;
};

/** Placement inputs for turning a footprint into a terrain flatten layer. */
struct FSWGFootprintFlattenParams
{
	/** Structure origin in terrain space (SWG world X/Y, metres). */
	FVector2D WorldCenter = FVector2D::ZeroVector;

	/** Structure yaw about the vertical axis, radians, applied about WorldCenter. */
	float YawRadians = 0.0f;

	/** Height the pad flattens to — the structure's base height. */
	float BaseHeight = 0.0f;

	/**
	 * Width of the ramp blending the pad edge into the surrounding terrain,
	 * IN METRES, measured inward from the pad boundary.
	 *
	 * Careful: FSWGTerrainBoundary::FeatheringAmount does not mean the same
	 * thing for every boundary type, and the generated pad is a Polygon.
	 * EvaluateBoundaryRectangle treats it as a 0-1 FRACTION of the rectangle's
	 * smaller dimension ("Inset = FeatheringAmount * MinDim * 0.5"), whereas
	 * EvaluateBoundaryPolygon treats it as an absolute DISTANCE
	 * ("sqrt(NearestSq) / FeatheringAmount"). Passing a fraction-shaped value
	 * like 0.25 here produces a 0.25 m ramp — i.e. a vertical-walled mesa.
	 *
	 * 0 means "derive one": half the smaller footprint cell, which tracks the
	 * 2-32 m range of cell sizes across retail's footprints.
	 */
	float FeatheringDistance = 0.0f;
	int32 FeatheringType = 0;
};

class SWGTRE_API FSWGFootprintReader
{
public:
	/** Parses a .sfp buffer (FORM FOOT). Returns false if the buffer isn't a recognised footprint. */
	static bool Read(const FSWGIffReader& Reader, FSWGStructureFootprint& OutFootprint);

	/**
	 * Synthesises the terrain layer a placed structure contributes: a polygon
	 * boundary around the footprint's 'F' region, carrying a single
	 * AffectorHeightConstant at Params.BaseHeight. Append the result to
	 * FSWGTerrainData::TopLevelLayers and the existing evaluator walk flattens
	 * the pad with no further changes — this is the same shape of data the
	 * .trn already stores for the pre-baked static-building pads.
	 *
	 * The boundary is the 'F' region's bounding rectangle, rotated by yaw. That
	 * is exact for the overwhelming majority of retail footprints (their 'F'
	 * regions are solid rectangles) but over-covers the handful with a concave
	 * 'F' region — shared_player_city_cityhall.sfp's cross shape is the clearest
	 * case. The overspill stays inside that footprint's own 'H' clearance ring,
	 * where nothing else may be placed anyway.
	 *
	 * Returns false when the footprint is invalid or has no 'F' cells.
	 */
	static bool BuildFlattenLayer(const FSWGStructureFootprint& Footprint, const FSWGFootprintFlattenParams& Params, FSWGTerrainLayer& OutLayer);
};
