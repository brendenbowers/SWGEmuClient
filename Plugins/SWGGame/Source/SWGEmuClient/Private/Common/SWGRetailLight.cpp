#include "Common/SWGRetailLight.h"

#include "Components/PointLightComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarRetailFalloff(
		TEXT("swg.Fx.PropLightRetailFalloff"), 0,
		TEXT("1 = point lights (props, .cef lights) use the retail attenuation curve with a hard range cutoff; 0 = UE inverse-square falloff."));

	TAutoConsoleVariable<float> CVarReferenceMetres(
		TEXT("swg.Fx.PropLightReferenceMetres"), 3.f,
		TEXT("Distance at which a retail-falloff light matches the inverse-square candela brightness."));

	TAutoConsoleVariable<float> CVarGain(
		TEXT("swg.Fx.PropLightRetailGain"), 1.f,
		TEXT("Extra brightness multiplier for retail-falloff lights."));
}

float SWGRetailLight::FitFalloffExponent(float Constant, float Linear, float Quadratic, float RangeMetres)
{
	const auto Denominator = [&](float Fraction)
	{
		const float Distance = Fraction * RangeMetres;
		return Constant + Linear * Distance + Quadratic * Distance * Distance;
	};
	const float Near = Denominator(0.1f);
	const float Far = Denominator(0.5f);
	if (Near <= KINDA_SMALL_NUMBER || Far <= KINDA_SMALL_NUMBER)
	{
		return 2.f;
	}
	const float Ratio = FMath::Clamp(Near / Far, 0.01f, 1.f);
	return FMath::Clamp(FMath::Loge(Ratio) / FMath::Loge(0.75f / 0.99f), 0.5f, 16.f);
}

float SWGRetailLight::ComputeIntensity(float Exponent, float Candelas, float RangeCm)
{
	if (CVarRetailFalloff.GetValueOnGameThread() == 0 || RangeCm <= KINDA_SMALL_NUMBER)
	{
		return Candelas;
	}
	// UPointLightComponent::ComputeLightBrightness: a candela light is cd * 100^2 / d_cm^2 = cd / d_m^2;
	// a unitless non-inverse light is intensity * curve(d). Match the two at the reference distance.
	const float ReferenceCm = FMath::Min(CVarReferenceMetres.GetValueOnGameThread() * 100.f, RangeCm * 0.5f);
	const float ReferenceMetres = ReferenceCm / 100.f;
	const float Window = FMath::Max(1.f - FMath::Square(ReferenceCm / RangeCm), KINDA_SMALL_NUMBER);
	return Candelas * CVarGain.GetValueOnGameThread() / (FMath::Square(ReferenceMetres) * FMath::Pow(Window, Exponent));
}

void SWGRetailLight::ApplyFalloff(UPointLightComponent& Light, float Exponent)
{
	const bool bInverseSquared = CVarRetailFalloff.GetValueOnGameThread() == 0;
	// The engine only forces Unitless for non-inverse lights on load/edit; ComputeIntensity assumes these units.
	const ELightUnits Units = bInverseSquared ? ELightUnits::Candelas : ELightUnits::Unitless;
	if (Light.IntensityUnits != Units)
	{
		Light.IntensityUnits = Units;
		Light.MarkRenderStateDirty();
	}
	if ((bool)Light.bUseInverseSquaredFalloff != bInverseSquared)
	{
		Light.SetUseInverseSquaredFalloff(bInverseSquared);
	}
	if (bInverseSquared && Light.SourceRadius <= 0.f)
	{
		Light.SetSourceRadius(5.f);
	}
	if (!FMath::IsNearlyEqual(Light.LightFalloffExponent, Exponent))
	{
		Light.SetLightFalloffExponent(Exponent);
	}
	if (Light.VolumetricScatteringIntensity > 0.f)
	{
		Light.SetVolumetricScatteringIntensity(0.f);
	}
}
