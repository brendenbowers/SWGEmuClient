#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGTerrainReader.h"

/** What one vegetation tier's affectors left at a point — see FSWGTerrainEvaluator::GetFlora. */
struct FSWGTerrainFloraSample
{
	/** 0 = nothing grows here. Indexes FSWGTerrainData::FloraFamilies or RadialFamilies depending on the tier asked for. */
	int32 FamilyId = 0;
	/** Chance (0..1) that a placement slot here is actually filled. */
	float Density = 0.0f;
	/** The terrain height at the point, from the same walk. */
	float Height = 0.0f;
};

/**
 * Ports Core3's ProceduralTerrainAppearance::getHeight/processTerrain and the
 * MVP Boundary/Affector process() methods 1:1 — see world-object-plan.html
 * "Terrain rendering" for every formula's exact source citation. No filter
 * types are parsed by FSWGTerrainReader yet, so the filter step from Core3's
 * processTerrain is a no-op here (nothing to run). Every affector currently
 * parsed is a height-type affector, so there's no "requestedType" parameter —
 * unlike Core3, which also uses this same framework for getEnvironmentID().
 */
class SWGTRE_API FSWGTerrainEvaluator
{
public:
	/**
	 * Confirmed port of ProceduralTerrainAppearance::getHeight(x,y).
	 * ExtraLayers are walked after Data.TopLevelLayers as if appended to them —
	 * runtime object modifications (building pads) live there so the parsed
	 * planet data can stay immutable and shared across bake threads.
	 */
	static float GetHeight(const FSWGTerrainData& Data, float X, float Y, TArrayView<const FSWGTerrainLayer> ExtraLayers = {});

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
	static void GetShaderWeights(const FSWGTerrainData& Data, float X, float Y, TMap<int32, float>& OutWeights, TArrayView<const FSWGTerrainLayer> ExtraLayers = {});

	/**
	 * The GetHeight walk with one vegetation tier's affectors (see
	 * SWGFloraTierAffectorType) applied alongside the height affectors, the
	 * way retail's single processTerrain pass did — so a height filter above
	 * a "no trees on peaks" layer sees the real running height, unlike
	 * GetShaderWeights' height-less walk. Returns the family/density left at
	 * (X,Y) plus the height itself, so a placement pass needs one walk per
	 * candidate point.
	 */
	static FSWGTerrainFloraSample GetFlora(const FSWGTerrainData& Data, float X, float Y, ESWGTerrainFloraTier Tier, TArrayView<const FSWGTerrainLayer> ExtraLayers = {});

private:
	/** Confirmed port of ProceduralTerrainAppearance::calculateFeathering. */
	static float CalculateFeathering(float Value, int32 FeatheringType);

	/**
	 * The tier being collected during a GetFlora walk — see ProcessLayer's
	 * Flora parameter. Also carries what the flora-only filters need: the
	 * shader paint so far (FSHD) and the ground slope at the point (FSLP),
	 * the latter costing two extra height walks and so found only on demand.
	 */
	struct FFloraWalk
	{
		const FSWGTerrainData& Data;
		TArrayView<const FSWGTerrainLayer> ExtraLayers;
		ESWGTerrainAffectorType AffectorType;
		/** Family ids resolve against RadialFamilies instead of FloraFamilies. */
		bool bRadial;
		FSWGTerrainFloraSample Sample;
		TMap<int32, float> ShaderWeights;
		/** The raw-space ground normal at the point, once a slope or direction filter has asked for it. */
		TOptional<FVector> Normal;
	};

	/** Confirmed port of ProceduralTerrainAppearance::processTerrain. Flora, when given, also applies that tier's flora affectors (ApplyFloraAffector) and tracks shader paint. */
	static float ProcessLayer(const FSWGTerrainLayer& Layer, float X, float Y, float& Height, float ParentTransform, const FSWGMapGroup& MapGroup, FFloraWalk* Flora = nullptr);

	/** See FSWGTerrainAffector's flora fields for the remove/replace/add semantics this applies. */
	static void ApplyFloraAffector(const FSWGTerrainAffector& Affector, float TransformValue, FFloraWalk& Flora);

	/** A ROAD-type road affector paints its shader family over the road rectangles it covers (see ProcessShaderLayer). */
	static void ApplyRoadShaderPaint(const FSWGTerrainAffector& Affector, float X, float Y, float Strength, TMap<int32, float>& OutWeights);

	/** The family with the most paint weight; 0 if nothing has been painted. */
	static int32 DominantShaderFamily(const TMap<int32, float>& Weights);

	/** The final ground normal at (X,Y) from two nearby height samples, found once per flora walk and cached in Flora.Normal. */
	static const FVector& GetWalkNormal(FFloraWalk& Flora, float X, float Y);

	/** FilterSlope/FilterDirection's shared band test: 1 inside [Min,Max] away from the edges, feathered to 0 across FeatheringAmount of the band at each end. */
	static float FeatheredBand(float Value, float Min, float Max, float FeatheringAmount);

	static float EvaluateBoundary(const FSWGTerrainBoundary& Boundary, float X, float Y);
	static float EvaluateBoundaryCircle(const FSWGTerrainBoundary& Boundary, float X, float Y);
	static float EvaluateBoundaryRectangle(const FSWGTerrainBoundary& Boundary, float X, float Y);
	static float EvaluateBoundaryPolygon(const FSWGTerrainBoundary& Boundary, float X, float Y);
	static float EvaluateBoundaryPolyline(const FSWGTerrainBoundary& Boundary, float X, float Y);

	/** Confirmed port of FilterHeight::process / FilterFractal::process; Slope and Shader filters need Flora and pass through (1.0) without it — see ESWGTerrainFilterType. */
	static float EvaluateFilter(const FSWGTerrainFilter& Filter, float X, float Y, float Height, const FSWGMapGroup& MapGroup, FFloraWalk* Flora = nullptr);

	static void ApplyAffector(const FSWGTerrainAffector& Affector, float X, float Y, float TransformValue, float& Height, const FSWGMapGroup& MapGroup);

	/** Confirmed port of processTerrain, specialized to the shader-weight walk (see GetShaderWeights). */
	static float ProcessShaderLayer(const FSWGTerrainLayer& Layer, float X, float Y, float& Height, float ParentTransform, const FSWGMapGroup& MapGroup, TMap<int32, float>& OutWeights);

	/** Applies one ShaderConstant/ShaderReplace affector's alpha-over paint to OutWeights — see GetShaderWeights' comment. */
	static void ApplyShaderAffector(const FSWGTerrainAffector& Affector, float TransformValue, TMap<int32, float>& OutWeights);

	/** Confirmed port of Segment::findNearestHeight — interpolates a road's baked height profile at (X,Y). */
	static void FindNearestRoadHeight(const FSWGTerrainRoadSegment& Segment, float& Height, float X, float Y, const FVector2D& RoadCenter, float Direction);
};
