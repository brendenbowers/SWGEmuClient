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

private:
	/** Confirmed port of ProceduralTerrainAppearance::calculateFeathering. */
	static float CalculateFeathering(float Value, int32 FeatheringType);

	/** Confirmed port of ProceduralTerrainAppearance::processTerrain. */
	static float ProcessLayer(const FSWGTerrainLayer& Layer, float X, float Y, float& Height, float ParentTransform, const FSWGMapGroup& MapGroup);

	static float EvaluateBoundary(const FSWGTerrainBoundary& Boundary, float X, float Y);
	static float EvaluateBoundaryCircle(const FSWGTerrainBoundary& Boundary, float X, float Y);
	static float EvaluateBoundaryRectangle(const FSWGTerrainBoundary& Boundary, float X, float Y);
	static float EvaluateBoundaryPolygon(const FSWGTerrainBoundary& Boundary, float X, float Y);

	static void ApplyAffector(const FSWGTerrainAffector& Affector, float X, float Y, float TransformValue, float& Height, const FSWGMapGroup& MapGroup);
};
