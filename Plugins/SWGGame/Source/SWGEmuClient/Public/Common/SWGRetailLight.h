#pragma once

#include "CoreMinimal.h"

class UPointLightComponent;

/**
 * Retail D3D lights fade as 1 / (c + l*d + q*d*d) and stop dead at the range. UE's non-inverse-square point light fades as
 * (1 - (d/R)^2)^exponent, so the exponent is fitted to the retail ratio between 0.1 R and 0.5 R, and the intensity is set so
 * the light matches the old candela (inverse-square) brightness at a reference distance. Inverse-square lights keep a bright,
 * wide skirt that reads as a halo; this does not. swg.Fx.PropLightRetailFalloff = 0 restores the inverse-square look.
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
