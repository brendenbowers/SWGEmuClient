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
};

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

	/** Polygon / Polyline */
	TArray<FVector2D> Vertices;

	/** Polyline only — the road/path's half-width, feathered per FeatheringAmount. */
	float LineWidth = 0.0f;
};

/**
 * FilterHeight (FHGT) and FilterFractal (FFRA) are modeled (confirmed via a
 * live diagnostic dump of tatooine.trn's actually-present layer child forms:
 * FHGT=37, FSLP=24, FFRA=23, FSHD=6, FDIR=2). Skipping filters entirely was
 * the root cause of an oversized terrain "cutout" with buildings appearing
 * sunk into it — Core3's processTerrain only ever lets a filter *shrink*
 * transformValue below what boundaries produced, never grow it, so a height
 * layer meant to be masked to a narrow height band (FilterHeight) or a noisy,
 * patchy sub-area (FilterFractal) was instead applying across its whole
 * (much larger) boundary shape. FilterFractal specifically was confirmed
 * (via a per-layer height trace at a live "spike" coordinate) as the cause of
 * isolated ~150-unit height spikes matching Hills/Mesa/Canyon/Erosion/
 * Outcropping-type layers applying at full (unfiltered) strength.
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

	// Diagnostic only (see chat: tracking down an oversized terrain "cutout"
	// with buildings sunk into it) — every boundary/filter FORM tag this
	// layer had that we don't parse (e.g. BPLN), so we can tell whether a
	// layer with real height affectors is missing a constraint that would
	// normally shrink where they apply.
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

struct FSWGTerrainData
{
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
};

/**
 * Parses SWG's .trn procedural terrain format (FORM PTAT) into an engine-agnostic
 * Layer/Boundary/Affector tree, mirroring Core3's terrain/layer/* class hierarchy
 * directly (confirmed field-for-field against Core3's parseFromIffStream methods —
 * see world-object-plan.html "Terrain rendering"). SGRP/FGRP/RGRP/EGRP (shader,
 * flora, radial, environment groups) are structurally skipped — not needed until
 * shader/flora rendering is tackled (explicitly deferred). The first MGRP (map
 * group) IS parsed, into FSWGTerrainData::MapGroup, since AffectorHeightFractal
 * needs it; a second MGRP occurrence (Core3's "bitmap group", a different,
 * bitmap-affector-only structure) is skipped. This class only extracts the graph
 * and resolves fractal noise generators; the recursive height-evaluation walk
 * (Core3's ProceduralTerrainAppearance::getHeight/processTerrain) is a separate,
 * not-yet-implemented step.
 */
class SWGEMU_API FSWGTerrainReader
{
public:
	/** Parses a .trn buffer (FORM PTAT). Returns false if the buffer isn't recognized. */
	static bool ReadTerrain(const FSWGIffReader& Reader, FSWGTerrainData& OutData);

private:
	static bool FindChildForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& FormType, FSWGIffChunk& OutChunk);
	static bool FindChildChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& Tag, FSWGIffChunk& OutChunk);
	/** All FORM children (any FormType) among Parent's direct children. */
	static TArray<FSWGIffChunk> FindChildForms(const FSWGIffReader& Reader, const FSWGIffChunk& Parent);
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

	static bool ReadAffectorHeightConstant(const FSWGIffReader& Reader, const FSWGIffChunk& AhcnForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorHeightFractal(const FSWGIffReader& Reader, const FSWGIffChunk& AhfrForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorHeightTerrace(const FSWGIffReader& Reader, const FSWGIffChunk& AhtrForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorRoad(const FSWGIffReader& Reader, const FSWGIffChunk& AroaForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorShaderConstant(const FSWGIffReader& Reader, const FSWGIffChunk& AscnForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorShaderReplace(const FSWGIffReader& Reader, const FSWGIffChunk& AsrpForm, FSWGTerrainAffector& OutAffector);

	/** Port of Segment::createRoadwayHeights — smooths a road's raw authored height samples via a sliding 3-point average before it's used for lookups. */
	static void BuildRoadwayHeights(FSWGTerrainRoadSegment& Segment);
	/** Port of AffectorRoad::generateRectangles/addNewRectangle — builds one oriented RoadRectangle per path segment (start/mid.../end) from RoadStartPoint/MidPositions/RoadEndPoint. */
	static void GenerateRoadRectangles(FSWGTerrainAffector& OutAffector, const TArray<FVector2D>& MidPositions, const FVector2D& EndPoint);

	/** FORM MGRP > FORM 0000 > FORM MFAM(*) > [DATA[familyId,name], FORM MFRC > FORM 0001 > DATA[fractal fields]]. */
	static bool ReadMapGroup(const FSWGIffReader& Reader, const FSWGIffChunk& MgrpForm, FSWGMapGroup& OutGroup);

	/** FORM SGRP > version form > SFAM(*) chunks — see swg.DumpShaderFamilies' original diagnostic for the confirmed wire layout this ports. */
	static bool ReadShadersGroup(const FSWGIffReader& Reader, const FSWGIffChunk& SgrpForm, TArray<FSWGShaderFamily>& OutFamilies);
};
