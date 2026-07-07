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
};

enum class ESWGTerrainBoundaryType : uint8
{
	Unknown,
	Circle,
	Rectangle,
	Polygon,
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

	/** Polygon */
	TArray<FVector2D> Vertices;
};

struct FSWGTerrainLayer
{
	FString Name;
	bool bEnabled = false;
	bool bInvertBoundaries = false;
	bool bInvertFilters = false;

	TArray<FSWGTerrainBoundary> Boundaries;
	TArray<FSWGTerrainAffector> Affectors;
	TArray<FSWGTerrainLayer> Children;
};

struct FSWGTerrainData
{
	/** Top-level layers, in file order — either a single root LAYR or LYRS's children. */
	TArray<FSWGTerrainLayer> TopLevelLayers;

	/** Resolved from the first MGRP (the "map group"); AffectorHeightFractal::FractalId indexes into this. */
	FSWGMapGroup MapGroup;
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

	static bool ReadAffectorHeightConstant(const FSWGIffReader& Reader, const FSWGIffChunk& AhcnForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorHeightFractal(const FSWGIffReader& Reader, const FSWGIffChunk& AhfrForm, FSWGTerrainAffector& OutAffector);
	static bool ReadAffectorHeightTerrace(const FSWGIffReader& Reader, const FSWGIffChunk& AhtrForm, FSWGTerrainAffector& OutAffector);

	/** FORM MGRP > FORM 0000 > FORM MFAM(*) > [DATA[familyId,name], FORM MFRC > FORM 0001 > DATA[fractal fields]]. */
	static bool ReadMapGroup(const FSWGIffReader& Reader, const FSWGIffChunk& MgrpForm, FSWGMapGroup& OutGroup);
};
