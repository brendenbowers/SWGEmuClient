#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGTerrainReader.h"

/**
 * Ports Core3's ProceduralTerrainAppearance::getHeight/processTerrain and the
 * MVP Boundary/Affector process() methods 1:1 — see world-object-plan.html
 * "Terrain rendering" for every formula's exact source citation. No filter
 * types are parsed by FSWGTerrainReader yet, so the filter step from Core3's
 * processTerrain is a no-op here (nothing to run). Every affector currently
 * parsed is a height-type affector, so there's no "requestedType" parameter —
 * unlike Core3, which also uses this same framework for getEnvironmentID().
 */
class SWGEMU_API FSWGTerrainEvaluator
{
public:
	/** Confirmed port of ProceduralTerrainAppearance::getHeight(x,y). */
	static float GetHeight(const FSWGTerrainData& Data, float X, float Y);

	/**
	 * Same layer-tree walk as GetHeight (identical boundary/filter/feathering
	 * framework), but only ShaderConstant/ShaderReplace affectors contribute —
	 * everything else (height affectors, road affectors) is skipped, matching
	 * Core3's own requestedType-filtered processTerrain dispatch (see
	 * ProceduralTerrainAppearance::getEnvironmentID for the analogous
	 * non-height traversal this mirrors). Returns each touched shader
	 * family's paint weight, summing to <= 1.0 — see ApplyShaderAffector for
	 * the alpha-over compositing model (Core3 has no reference process()
	 * implementation for these two affector types to port, since shader
	 * painting was purely a client rendering concern).
	 */
	static void GetShaderWeights(const FSWGTerrainData& Data, float X, float Y, TMap<int32, float>& OutWeights);

	/** Temporary diagnostic: when enabled, GetHeight/ProcessLayer log every layer's name, TransformValue, and Height delta for calls at (X,Y) (within a small epsilon). */
	static void SetDebugTraceTarget(float X, float Y, bool bEnable);

private:
	/** Confirmed port of ProceduralTerrainAppearance::calculateFeathering. */
	static float CalculateFeathering(float Value, int32 FeatheringType);

	/** Confirmed port of ProceduralTerrainAppearance::processTerrain. */
	static float ProcessLayer(const FSWGTerrainLayer& Layer, float X, float Y, float& Height, float ParentTransform, const FSWGMapGroup& MapGroup);

	static float EvaluateBoundary(const FSWGTerrainBoundary& Boundary, float X, float Y);
	static float EvaluateBoundaryCircle(const FSWGTerrainBoundary& Boundary, float X, float Y);
	static float EvaluateBoundaryRectangle(const FSWGTerrainBoundary& Boundary, float X, float Y);
	static float EvaluateBoundaryPolygon(const FSWGTerrainBoundary& Boundary, float X, float Y);
	static float EvaluateBoundaryPolyline(const FSWGTerrainBoundary& Boundary, float X, float Y);

	/** Confirmed port of FilterHeight::process / FilterFractal::process (see FSWGTerrainFilter's comment for which types are modeled). */
	static float EvaluateFilter(const FSWGTerrainFilter& Filter, float X, float Y, float Height, const FSWGMapGroup& MapGroup);

	static void ApplyAffector(const FSWGTerrainAffector& Affector, float X, float Y, float TransformValue, float& Height, const FSWGMapGroup& MapGroup);

	/** Confirmed port of processTerrain, specialized to the shader-weight walk (see GetShaderWeights). */
	static float ProcessShaderLayer(const FSWGTerrainLayer& Layer, float X, float Y, float& Height, float ParentTransform, const FSWGMapGroup& MapGroup, TMap<int32, float>& OutWeights);

	/** Applies one ShaderConstant/ShaderReplace affector's alpha-over paint to OutWeights — see GetShaderWeights' comment. */
	static void ApplyShaderAffector(const FSWGTerrainAffector& Affector, float TransformValue, TMap<int32, float>& OutWeights);

	/** Confirmed port of Segment::findNearestHeight — interpolates a road's baked height profile at (X,Y). */
	static void FindNearestRoadHeight(const FSWGTerrainRoadSegment& Segment, float& Height, float X, float Y, const FVector2D& RoadCenter, float Direction);
};
