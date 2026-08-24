#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGTerrainReader.h"

/**
 * Where a runtime-placed object sits, for the purpose of stamping its terrain
 * modification into the live layer graph.
 */
struct FSWGTerrainPlacement
{
	/** Object origin in terrain space (SWG world X/Y, metres). */
	FVector2D WorldCenter = FVector2D::ZeroVector;

	/** Object yaw about the vertical axis, radians, applied about WorldCenter. */
	float YawRadians = 0.0f;

	/** Ground height the object was placed at. Layer heights are authored relative to this. */
	float BaseHeight = 0.0f;
};

/**
 * Turns an object-local terrain modification (a parsed .lay, or a layer
 * synthesised from a .sfp footprint) into world-space layers that can be
 * appended to a live FSWGTerrainData and evaluated by the existing
 * FSWGTerrainEvaluator walk with no further special-casing.
 */
class SWGTRE_API FSWGTerrainModifier
{
public:
	/** Transforms Layer and all its descendants from object-local space into world space, in place. */
	static void TransformLayer(FSWGTerrainLayer& Layer, const FSWGTerrainPlacement& Placement);

	/**
	 * World-space XY extent covered by Layer's boundaries, including its
	 * descendants'. Used to work out which baked terrain tiles a modification
	 * dirties. Returns false when the layer tree has no boundary with a usable
	 * extent (an unbounded layer, which would dirty everything).
	 */
	static bool GetLayerWorldBounds(const FSWGTerrainLayer& Layer, FBox2D& OutBounds);
};
