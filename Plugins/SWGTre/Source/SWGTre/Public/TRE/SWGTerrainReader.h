#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGTerrainNoise.h"

/**
 * MVP subset only — the majority-coverage affector/boundary types confirmed against
 * Core3's terrain source (world-object-plan.html "Terrain rendering"). Every other
 * affector/boundary/filter tag is parsed-and-skipped, matching Core3's own graceful
 * fallback for unrecognized types (Layer::parseAffector/parseBoundary return nullptr).
 */
enum class ESWGTerrainAffectorType : uint8
{
	Unknown,
	HeightConstant,
	HeightFractal,
	HeightTerrace,
	Road,
	ShaderConstant,
	ShaderReplace,
	/** AFSC / AFSN / AFDN / AFDF — see FSWGTerrainAffector's flora fields. */
	FloraCollidable,
	FloraNonCollidable,
	RadialNear,
	RadialFar,
};

/**
 * The four vegetation tiers the retail client streamed independently, each
 * with its own header parameters (FSWGTerrainHeader::FVegetationTier), its
 * own affector tag, and its own family table: the two flora tiers place
 * FSWGFloraFamily meshes, the two radial tiers place FSWGRadialFamily
 * billboards.
 */
enum class ESWGTerrainFloraTier : uint8
{
	Collidable,
	NonCollidable,
	RadialNear,
	RadialFar,
	Count
};

inline bool SWGIsRadialFloraTier(ESWGTerrainFloraTier Tier)
{
	return Tier == ESWGTerrainFloraTier::RadialNear || Tier == ESWGTerrainFloraTier::RadialFar;
}

inline ESWGTerrainAffectorType SWGFloraTierAffectorType(ESWGTerrainFloraTier Tier)
{
	switch (Tier)
	{
		case ESWGTerrainFloraTier::Collidable: return ESWGTerrainAffectorType::FloraCollidable;
		case ESWGTerrainFloraTier::NonCollidable: return ESWGTerrainAffectorType::FloraNonCollidable;
		case ESWGTerrainFloraTier::RadialNear: return ESWGTerrainAffectorType::RadialNear;
		default: return ESWGTerrainAffectorType::RadialFar;
	}
}

/** Port of Core3's Segment (terrain/layer/Segment.h) — one road's baked, hand-authored height profile along its path (X,Z,Y wire order, matching every other position field in this format). */
struct FSWGTerrainRoadPoint
{
	float X = 0.0f, Z = 0.0f, Y = 0.0f;
};

struct FSWGTerrainRoadSegment
{
	TArray<FSWGTerrainRoadPoint> Positions;
	/** True when consecutive Z values are identical — Segment::findNearestHeight uses the
	 *  first point's height directly instead of interpolating along the path. */
	bool bFlatRoad = false;
};

/** Port of Core3's RoadRectangle (AffectorRoad.h) — one road segment's oriented bounding box, used for the "is this point on the road" test. */
struct FSWGTerrainRoadRectangle
{
	float CenterX = 0.0f, CenterY = 0.0f;
	float Width = 0.0f, Height = 0.0f;
	float Direction = 0.0f;
	float RoadStartX = 0.0f, RoadStartY = 0.0f;
};

enum class ESWGTerrainBoundaryType : uint8
{
	Unknown,
	Circle,
	Rectangle,
	Polygon,
	Polyline,
};

enum class ESWGTerrainFilterType : uint8
{
	Unknown,
	Height,
	Fractal,
	/**
	 * FSLP / FSHD — only evaluated on the flora walk (FSWGTerrainEvaluator::
	 * GetFlora), where the ground's final slope and painted shader are
	 * knowable; the height walk passes them through, since a height layer
	 * gated by the slope of the height it is producing has no per-point
	 * answer (retail evaluated it against the chunk's running height buffer).
	 */
	Slope,
	Shader,
	/** FDIR — the compass direction a slope faces; flora-walk only, like Slope. */
	Direction,
};

/** Fields are a union across the MVP affector types — only the ones relevant to Type are meaningful. */
struct FSWGTerrainAffector
{
	ESWGTerrainAffectorType Type = ESWGTerrainAffectorType::Unknown;
	bool bEnabled = false;

	/** HeightConstant / HeightFractal: 1=add,2=subtract,3=scale,4=zero,default=lerp (see AffectorHeightConstant::process). */
	int32 OperationType = 0;
	/** HeightConstant / HeightFractal / HeightTerrace: constant height, noise amplitude, or terrace step size respectively. */
	float Height = 0.0f;

	/** HeightFractal only — index into FSWGTerrainData::MapGroup (FSWGMapGroup::FindFractal). */
	int32 FractalId = 0;

	/** HeightTerrace only — flat-vs-slope portion of each terrace step. */
	float FlatRatio = 0.0f;

	/**
	 * Road only — Core3's AffectorRoad packs either a shader-only 'ROAD' sub-form
	 * (texture/visual, doesn't touch height — out of scope) or a 'HDTA' (HeightData)
	 * sub-form (a baked height profile the road follows) inside the same wire slot;
	 * only HDTA affects GetHeight, matching AffectorRoad::process's own
	 * "type != 'HDTA' -> return" early-out. When false, this affector is a
	 * legitimate no-op for height purposes (same as any disabled affector).
	 */
	bool bRoadIsHeightType = false;
	TArray<FSWGTerrainRoadSegment> RoadSegments;
	TArray<FSWGTerrainRoadRectangle> RoadRectangles;
	FVector2D RoadStartPoint = FVector2D::ZeroVector;
	/** Already has Core3's own "* 0.7f" fudge factor applied (see AffectorRoad::parseFromIffStream). */
	float RoadWidth = 0.0f;

	/**
	 * ShaderConstant/ShaderReplace (ASCN/ASRP) — ported from Core3's own wire
	 * format (AffectorShaderConstant/AffectorShaderReplace), but Core3 itself
	 * never implements process() for either (shader painting is purely a
	 * client rendering concern the server has no use for), so there's no
	 * reference algorithm to port for *how* these affect the shader-weight
	 * map — FSWGTerrainEvaluator::ApplyShaderAffector models it as a
	 * standard alpha-over paint using the same TransformValue this framework
	 * already produces from boundaries/filters/feathering.
	 */
	int32 ShaderFamilyId = 0;         // ShaderConstant: family this layer paints.
	int32 ShaderOldFamilyId = 0;      // ShaderReplace: family to paint over.
	int32 ShaderNewFamilyId = 0;      // ShaderReplace: family replacing it.
	int32 ShaderFeatheringType = 0;
	float ShaderFeatheringAmount = 0.0f;

	/**
	 * FloraCollidable/FloraNonCollidable/RadialNear/RadialFar (AFSC/AFSN v0004,
	 * AFDN/AFDF v0002) — same five-field DATA for all four. Core3 parses the
	 * chunk but names the last two "featheringType/featheringAmount", which
	 * the shipped data contradicts: the float is exactly 1.0 whenever the
	 * flag is 0 and a 0..1 fraction whenever it is 1, and the flag never
	 * exceeds 1 — a density override, matching the retail terrain editor's
	 * fields. Where the layer's transform is > 0: bFloraRemoveAll clears the
	 * tier's family at that point, otherwise FloraFamilyId is written
	 * (operation 1 = only where nothing is set yet; 0 = unconditionally)
	 * with FloraDensity or the family's own density.
	 */
	int32 FloraFamilyId = 0;
	int32 FloraOperation = 0;
	bool bFloraRemoveAll = false;
	bool bFloraDensityOverride = false;
	float FloraDensity = 1.0f;
};

/** Fields are a union across the MVP boundary types — only the ones relevant to Type are meaningful. */
struct FSWGTerrainBoundary
{
	ESWGTerrainBoundaryType Type = ESWGTerrainBoundaryType::Unknown;
	bool bEnabled = false;
	int32 FeatheringType = 0;
	float FeatheringAmount = 0.0f;

	/** Circle */
	float CenterX = 0.0f, CenterY = 0.0f, Radius = 0.0f;

	/** Rectangle */
	float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f;

	/** Rectangle / Polygon (version-dependent — absent on older Rectangle v0002) */
	bool bLocalWaterTableEnabled = false;
	float LocalWaterTableHeight = 0.0f;
	/** Bare shader name for the water surface (e.g. "wter_spec" -> shader/wter_spec.sht) and its UV tiling size in metres. */
	FString LocalWaterTableShader;
	float LocalWaterTableShaderSize = 0.0f;

	/** Polygon / Polyline */
	TArray<FVector2D> Vertices;

	/** Polyline only — the road/path's half-width, feathered per FeatheringAmount. */
	float LineWidth = 0.0f;
};

/**
 * FilterHeight (FHGT) and FilterFractal (FFRA) — Core3's processTerrain only
 * ever lets a filter *shrink* transformValue below what boundaries produced,
 * never grow it, so skipping filters lets a layer meant to be masked to a
 * narrow band or patchy sub-area apply across its whole boundary shape instead.
 */
struct FSWGTerrainFilter
{
	ESWGTerrainFilterType Type = ESWGTerrainFilterType::Unknown;
	bool bEnabled = false;
	int32 FeatheringType = 0;
	float FeatheringAmount = 0.0f;

	/** Height only. */
	float MinHeight = 0.0f;
	float MaxHeight = 0.0f;

	/** Fractal only — index into FSWGTerrainData::MapGroup (FSWGMapGroup::FindFractal). */
	int32 FractalId = 0;
	/** Fractal only — noise*Scale must fall within (Min,Max) for this filter to pass. */
	float Min = 0.0f;
	float Max = 0.0f;
	float Scale = 0.0f;

	/** Slope only — the ground's angle from horizontal must fall within [Min,Max] degrees (FilterSlope's own clamping to 0..90 applied on read). */
	float SlopeMinAngleDegrees = 0.0f;
	float SlopeMaxAngleDegrees = 90.0f;

	/** Shader only — the family painted at the point (FSWGTerrainEvaluator::GetShaderWeights' dominant id) must be this one. */
	int32 ShaderFamilyId = 0;

	/**
	 * Direction only — atan2(north, east) of the ground normal, in degrees
	 * (-180..180; flat ground reads 0), must fall within [Min,Max]. The
	 * shipped values are degrees (Tatooine: -140..140, -45..45); Core3's
	 * FilterDirection clamps them as if radians, which is a bug there.
	 */
	float DirectionMinDegrees = -180.0f;
	float DirectionMaxDegrees = 180.0f;
};

struct FSWGTerrainLayer
{
	FString Name;
	bool bEnabled = false;
	bool bInvertBoundaries = false;
	bool bInvertFilters = false;

	TArray<FSWGTerrainBoundary> Boundaries;
	TArray<FSWGTerrainFilter> Filters;
	TArray<FSWGTerrainAffector> Affectors;
	TArray<FSWGTerrainLayer> Children;

	// Diagnostic only: every boundary/filter FORM tag this layer had that we
	// don't parse (e.g. BPLN), to tell whether a layer with real height
	// affectors is missing a constraint that would normally shrink where they apply.
	TArray<FString> SkippedBoundaryOrFilterTags;
};

/**
 * Port of Core3's ShaderFamily (terrain/ShaderFamily.h) — one paintable ground
 * texture family, referenced by AffectorShaderConstant/AffectorShaderReplace's
 * familyId. LayerNames map directly (confirmed live via swg.FindVirtualPaths)
 * to TRE virtual paths texture/<name>.dds (diffuse) and texture/<name>_n.dds
 * (normal) — no .sht shader-template parsing needed. Only the first (primary)
 * layer name is actually used for now; a family's remaining layer entries are
 * detail/blend sub-textures the original client's shader combined internally,
 * out of scope for this MVP vertex-color blend.
 */
struct FSWGShaderFamily
{
	int32 FamilyId = 0;
	FString Name;
	TArray<FString> LayerNames;
};

/**
 * PTAT > <version> > DATA — the planet-wide constants. Port of the head of
 * Core3's ProceduralTerrainAppearance::parseFromIffStream; every shipped
 * planet reads size 16384, chunk 8, tilesPerChunk 2 (a 2 m pole spacing).
 * The flora/radial blocks that follow are the retail client's own vegetation
 * streaming radii and are parsed for when that work arrives.
 */
struct FSWGTerrainHeader
{
	/** Playable extent along each axis in raw units — the map spans +/- MapSize/2. */
	float MapSize = 16384.0f;

	/** Retail's smallest generation chunk, and the unit larger LOD chunks are powers of two of. */
	float ChunkSize = 8.0f;
	uint32 TilesPerChunk = 2;

	/** Core3's getDistanceBetweenPoles — the finest vertex spacing retail rendered. */
	float GetPoleSpacing() const { return TilesPerChunk > 0 ? ChunkSize / (TilesPerChunk * 2.0f) : ChunkSize; }

	bool bUseGlobalWaterTable = false;
	float GlobalWaterTableHeight = 0.0f;
	float GlobalWaterTableShaderSize = 0.0f;
	FString GlobalWaterTableShader;

	float TimeCycle = 0.0f;

	/**
	 * v0013 only (dev/test maps — every shipped planet is v0014). Four
	 * (shader name, size) pairs in the same shape as the global water table's
	 * shader + size, then a flag and a name. Core3 discards them; every v0013
	 * file in the archives holds empty names with 0.5/0.7/0.5/0.7, 0, "" —
	 * so their exact meaning is unconfirmed and nothing reads them yet.
	 */
	struct FLegacyV13
	{
		TArray<FString> ShaderNames;
		TArray<float> ShaderSizes;
		uint32 Flag = 0;
		FString Name;
	};
	TOptional<FLegacyV13> LegacyV13;

	/**
	 * One vegetation tier's streaming parameters, as retail's client used them:
	 * flora is placed per TileSize-metre tile (plus TileBorder of overlap so
	 * neighbours don't pop at the seam), from a deterministic RNG seeded by
	 * Seed and the tile coordinate, drawn between Min and MaxDistance.
	 */
	struct FVegetationTier
	{
		float MinDistance = 0.0f;
		float MaxDistance = 0.0f;
		float TileSize = 0.0f;
		float TileBorder = 0.0f;
		uint32 Seed = 0;
	};

	FVegetationTier FloraCollidable;
	FVegetationTier FloraNonCollidable;
	FVegetationTier RadialNear;
	FVegetationTier RadialFar;
};

/** One appearance a flora family can place — a child of FSWGFloraFamily. */
struct FSWGFloraChild
{
	/** Bare file name (e.g. "succ_tatt_hubba_lrg.apt", occasionally a ".prt" particle effect), under appearance/. */
	FString AppearanceName;
	/** Relative pick weight among the family's children. */
	float Weight = 1.0f;
	bool bShouldSway = false;
	float SwayDisplacement = 0.0f;
	float SwayPeriod = 0.0f;
	/** Tilt the instance to the ground normal instead of standing it straight up. */
	bool bAlignToTerrain = false;
	bool bShouldScale = false;
	float MinScale = 1.0f;
	float MaxScale = 1.0f;
};

/**
 * FGRP > 0008 > FFAM — one mesh-flora family (trees, rocks, shrubs),
 * referenced by AFSC/AFSN affectors' FloraFamilyId. Confirmed against the
 * shipped data: children carry real .apt names, weights, and 0.x..2.0 scale
 * ranges. Core3's FloraFamily only names the fields var1..var8.
 */
struct FSWGFloraFamily
{
	int32 FamilyId = 0;
	FString Name;
	FColor Color = FColor::White;
	/** Chance (0..1) a placement slot inside this family's area holds a plant, unless the affector overrides it. */
	float Density = 1.0f;
	bool bFloatsOnWater = false;
	TArray<FSWGFloraChild> Children;

	/** Weighted pick by a 0..1 roll; null if the family has no children. */
	const FSWGFloraChild* PickChild(float UnitRoll) const;
};

/** One billboard a radial family can place — a child of FSWGRadialFamily. */
struct FSWGRadialChild
{
	/** Bare shader name (e.g. "radl_grss_dsrt_tuft") — shader/<name>.sht, whose texture is texture/<name>.dds. */
	FString ShaderName;
	float Weight = 1.0f;
	/** Retail's per-child fade distance; every shipped entry reads 10. */
	float Distance = 0.0f;
	float MinWidth = 1.0f;
	float MaxWidth = 1.0f;
	/** Height follows the texture's aspect ratio; otherwise the billboard is as tall as it is wide. */
	bool bMaintainAspectRatio = false;
	float SwayPeriod = 0.0f;
	float SwayDisplacement = 0.0f;
	bool bShouldSway = false;
	bool bAlignToTerrain = false;
};

/** RGRP > 0003 > RFAM — one billboard-flora family (grass tufts, flowers, far-field tree sprites), referenced by AFDN/AFDF affectors' FloraFamilyId. */
struct FSWGRadialFamily
{
	int32 FamilyId = 0;
	FString Name;
	FColor Color = FColor::White;
	float Density = 1.0f;
	TArray<FSWGRadialChild> Children;

	/** Weighted pick by a 0..1 roll; null if the family has no children. */
	const FSWGRadialChild* PickChild(float UnitRoll) const;
};

struct FSWGTerrainData
{
	FSWGTerrainHeader Header;

	/** Top-level layers, in file order — either a single root LAYR or LYRS's children. */
	TArray<FSWGTerrainLayer> TopLevelLayers;

	/** Resolved from the first MGRP (the "map group"); AffectorHeightFractal::FractalId indexes into this. */
	FSWGMapGroup MapGroup;

	/** Resolved from the first SGRP (the "shaders group"); AffectorShaderConstant/Replace's familyId indexes into this. */
	TArray<FSWGShaderFamily> ShaderFamilies;

	const FSWGShaderFamily* FindShaderFamily(int32 FamilyId) const
	{
		return ShaderFamilies.FindByPredicate([FamilyId](const FSWGShaderFamily& F) { return F.FamilyId == FamilyId; });
	}

	/** From FGRP (AFSC/AFSN FloraFamilyId) and RGRP (AFDN/AFDF FloraFamilyId). */
	TArray<FSWGFloraFamily> FloraFamilies;
	TArray<FSWGRadialFamily> RadialFamilies;

	const FSWGFloraFamily* FindFloraFamily(int32 FamilyId) const
	{
		return FloraFamilies.FindByPredicate([FamilyId](const FSWGFloraFamily& Family) { return Family.FamilyId == FamilyId; });
	}

	const FSWGRadialFamily* FindRadialFamily(int32 FamilyId) const
	{
		return RadialFamilies.FindByPredicate([FamilyId](const FSWGRadialFamily& Family) { return Family.FamilyId == FamilyId; });
	}

	const FSWGTerrainHeader::FVegetationTier& GetVegetationTier(ESWGTerrainFloraTier Tier) const
	{
		switch (Tier)
		{
			case ESWGTerrainFloraTier::Collidable: return Header.FloraCollidable;
			case ESWGTerrainFloraTier::NonCollidable: return Header.FloraNonCollidable;
			case ESWGTerrainFloraTier::RadialNear: return Header.RadialNear;
			default: return Header.RadialFar;
		}
	}
};

/**
 * Parses SWG's .trn procedural terrain format (FORM PTAT) into an engine-agnostic
 * Layer/Boundary/Affector tree, mirroring Core3's terrain/layer/* class hierarchy
 * directly (confirmed field-for-field against Core3's parseFromIffStream methods —
 * see world-object-plan.html "Terrain rendering"). SGRP/FGRP/RGRP (shader,
 * flora, radial family tables) are parsed; EGRP (environment group) is
 * structurally skipped. The first MGRP (map
 * group) IS parsed, into FSWGTerrainData::MapGroup, since AffectorHeightFractal
 * needs it; a second MGRP occurrence (Core3's "bitmap group", a different,
 * bitmap-affector-only structure) is skipped. This class only extracts the graph
 * and resolves fractal noise generators; the recursive height-evaluation walk
 * (Core3's ProceduralTerrainAppearance::getHeight/processTerrain) is a separate,
 * not-yet-implemented step.
 */
class SWGTRE_API FSWGTerrainReader
{
public:
	/** Parses a .trn buffer (FORM PTAT). Returns false if the buffer isn't recognized. */
	static bool ReadTerrain(const FSWGIffReader& Reader, FSWGTerrainData& OutData);

	/**
	 * Parses a .lay buffer — the target of a shared object template's
	 * "terrainModificationFileName", i.e. the terrain edit a runtime-placed
	 * object applies to the ground it lands on. 468 templates carry one, 451 of
	 * them POIs (camps, lairs, ruins); 46 distinct .lay files exist in the TREs.
	 *
	 * Structurally it is a .trn's layer graph with the PTAT/TGEN wrapper
	 * removed: SGRP/FGRP/RGRP/EGRP/MGRP stubs and one or more LAYR forms, all
	 * as top-level siblings. terrain/thm_tatt_mos_imprv_building02_s01.lay for
	 * instance is a single layer "Tatooine Filler Building02_s01" holding one
	 * BoundaryRectangle (+/-14.03 m, feathered) and one AffectorHeightConstant.
	 *
	 * Coordinates and heights are LOCAL to the placed object — transform the
	 * result into world space before appending it to a live FSWGTerrainData.
	 */
	static bool ReadLayerFile(const FSWGIffReader& Reader, FSWGTerrainData& OutData);

private:
	/** PTAT > <version> > DATA — see FSWGTerrainHeader. Skips the v0013-only block and stops cleanly on a short chunk. */
	static bool ReadHeader(const FSWGIffReader& Reader, const FSWGIffChunk& PtatVersionForm, FSWGTerrainHeader& OutHeader);

	static FString ReadNullTerminatedStringAt(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, int32 Offset);

	/** FORM IHDR > FORM 0001 > DATA[int32 enabled][string name]. Shared by Layer, every Boundary, every Affector. */
	static bool ReadInformationHeader(const FSWGIffReader& Reader, const FSWGIffChunk& IhdrForm, FString& OutName, bool& bOutEnabled);

	/** FORM LAYR > FORM 0003 > [IHDR, ADTA, then boundary/affector/nested-LAYR children in file order]. */
	static bool ReadLayer(const FSWGIffReader& Reader, const FSWGIffChunk& LayrForm, FSWGTerrainLayer& OutLayer);

	/** Dispatches on BoundaryForm's FormType (BCIR/BREC/BPOL); returns false (leaves OutBoundary untouched) for unrecognized types. */
	static bool ReadBoundary(const FSWGIffReader& Reader, const FSWGIffChunk& BoundaryForm, FSWGTerrainBoundary& OutBoundary);

	/** Dispatches on AffectorForm's FormType (AHCN/AHFR/AHTR); returns false (leaves OutAffector untouched) for unrecognized types. */
	static bool ReadAffector(const FSWGIffReader& Reader, const FSWGIffChunk& AffectorForm, FSWGTerrainAffector& OutAffector);

	static bool ReadBoundaryCircle(const FSWGIffReader& Reader, const FSWGIffChunk& BcirForm, FSWGTerrainBoundary& OutBoundary);
	static bool ReadBoundaryRectangle(const FSWGIffReader& Reader, const FSWGIffChunk& BrecForm, FSWGTerrainBoundary& OutBoundary);
	static bool ReadBoundaryPolygon(const FSWGIffReader& Reader, const FSWGIffChunk& BpolForm, FSWGTerrainBoundary& OutBoundary);
	static bool ReadBoundaryPolyline(const FSWGIffReader& Reader, const FSWGIffChunk& BplnForm, FSWGTerrainBoundary& OutBoundary);

	/** Dispatches on FilterForm's FormType (FHGT, FFRA); returns false (leaves OutFilter untouched) for unrecognized types. */
	static bool ReadFilter(const FSWGIffReader& Reader, const FSWGIffChunk& FilterForm, FSWGTerrainFilter& OutFilter);
	static bool ReadFilterHeight(const FSWGIffReader& Reader, const FSWGIffChunk& FhgtForm, FSWGTerrainFilter& OutFilter);
	static bool ReadFilterFractal(const FSWGIffReader& Reader, const FSWGIffChunk& FfraForm, FSWGTerrainFilter& OutFilter);
	/** FSLP v0002: [minAngle:deg][maxAngle:deg][featheringType][featheringAmount] — Core3 FilterSlope. */
	static bool ReadFilterSlope(const FSWGIffReader& Reader, const FSWGIffChunk& FslpForm, FSWGTerrainFilter& OutFilter);
	/** FSHD v0000: [shaderFamilyId] — Core3 FilterShader. */
	static bool ReadFilterShader(const FSWGIffReader& Reader, const FSWGIffChunk& FshdForm, FSWGTerrainFilter& OutFilter);
	/** FDIR v0000: [minDegrees][maxDegrees][featheringType][featheringAmount] — Core3 FilterDirection. */
	static bool ReadFilterDirection(const FSWGIffReader& Reader, const FSWGIffChunk& FdirForm, FSWGTerrainFilter& OutFilter);

	static bool ReadAffectorHeightConstant(const FSWGIffReader& Reader, const FSWGIffChunk& AhcnForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorHeightFractal(const FSWGIffReader& Reader, const FSWGIffChunk& AhfrForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorHeightTerrace(const FSWGIffReader& Reader, const FSWGIffChunk& AhtrForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorRoad(const FSWGIffReader& Reader, const FSWGIffChunk& AroaForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorShaderConstant(const FSWGIffReader& Reader, const FSWGIffChunk& AscnForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorShaderReplace(const FSWGIffReader& Reader, const FSWGIffChunk& AsrpForm, FSWGTerrainAffector& OutAffector);

	/** AFSC/AFSN (v0004) and AFDN/AFDF (v0002) share one DATA layout — see FSWGTerrainAffector's flora fields. */
	static bool ReadAffectorFlora(const FSWGIffReader& Reader, const FSWGIffChunk& AffectorForm, FSWGIffTag VersionTag, ESWGTerrainAffectorType Type, FSWGTerrainAffector& OutAffector);

	/** Port of Segment::createRoadwayHeights — smooths a road's raw authored height samples via a sliding 3-point average before it's used for lookups. */
	static void BuildRoadwayHeights(FSWGTerrainRoadSegment& Segment);
	/** Port of AffectorRoad::generateRectangles/addNewRectangle — builds one oriented RoadRectangle per path segment (start/mid.../end) from RoadStartPoint/MidPositions/RoadEndPoint. */
	static void GenerateRoadRectangles(FSWGTerrainAffector& OutAffector, const TArray<FVector2D>& MidPositions, const FVector2D& EndPoint);

	/** FORM MGRP > FORM 0000 > FORM MFAM(*) > [DATA[familyId,name], FORM MFRC > FORM 0001 > DATA[fractal fields]]. */
	static bool ReadMapGroup(const FSWGIffReader& Reader, const FSWGIffChunk& MgrpForm, FSWGMapGroup& OutGroup);

	/** FORM SGRP > version form > SFAM(*) chunks — see swg.DumpShaderFamilies' original diagnostic for the confirmed wire layout this ports. */
	static bool ReadShadersGroup(const FSWGIffReader& Reader, const FSWGIffChunk& SgrpForm, TArray<FSWGShaderFamily>& OutFamilies);

	/** FORM FGRP > 0008 > FFAM(*) chunks — Core3's FloraGroup/FloraFamily wire layout, see FSWGFloraFamily for what the fields are. */
	static bool ReadFloraGroup(const FSWGIffReader& Reader, const FSWGIffChunk& FgrpForm, TArray<FSWGFloraFamily>& OutFamilies);

	/** FORM RGRP > 0003 > RFAM(*) chunks — Core3's RadialGroup/RadialFamily wire layout, see FSWGRadialFamily. */
	static bool ReadRadialGroup(const FSWGIffReader& Reader, const FSWGIffChunk& RgrpForm, TArray<FSWGRadialFamily>& OutFamilies);
};
