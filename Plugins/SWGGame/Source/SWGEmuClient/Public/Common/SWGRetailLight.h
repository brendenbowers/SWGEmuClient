#pragma once

#include "CoreMinimal.h"

class UPointLightComponent;

/**
 * Uses Unreal's physical inverse-square point lights by default. The optional retail mode fits
 * (1 - (d/R)^2)^exponent to SWG's 1 / (c + l*d + q*d*d), matching brightness at a reference distance.
 * swg.Fx.PropLightRetailFalloff = 1 selects the fitted retail curve.
 */
namespace SWGRetailLight
{
	/** Exponent for a retail attenuation triple over the range (metres). 2 when the triple is degenerate. */
	SWGEMUCLIENT_API float FitFalloffExponent(float Constant, float Linear, float Quadratic, float RangeMetres);

	/** Intensity to give the light for the active falloff mode, from the candela brightness it would have as an inverse-square light. */
	SWGEMUCLIENT_API float ComputeIntensity(float Exponent, float Candelas, float RangeCm);

	/** Switches the light between inverse-square and the fitted retail curve per the cvar, and hides it from volumetric fog. */
	SWGEMUCLIENT_API void ApplyFalloff(UPointLightComponent& Light, float Exponent);
}
