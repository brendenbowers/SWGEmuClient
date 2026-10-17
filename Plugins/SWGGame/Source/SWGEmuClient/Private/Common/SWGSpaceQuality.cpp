#include "Common/SWGSpaceQuality.h"
#include "HAL/IConsoleManager.h"

namespace
{
	void OnQualityChanged(IConsoleVariable*) { FSWGSpaceQuality::Apply(); }

	TAutoConsoleVariable<int32> CVarSpaceQuality(
		TEXT("swg.Space.Quality"), FSWGSpaceQuality::MaxTier,
		TEXT("Space visual quality 0-3 (3 = 4K, high-end GPU). Sets swg.Space.NebulaQuality, NebulaWisps, DustCountScale, Debris, PlanetRegionResolution and HudFlat; "
			"a feature cvar set explicitly (console, code, ini) keeps its own value."),
		FConsoleVariableDelegate::CreateStatic(&OnQualityChanged));

	// Index = tier. Tier 3 equals the retail-faithful defaults of each feature cvar.
	const FSWGSpaceQualityTier Tiers[FSWGSpaceQuality::TierCount] = {
		{0, false, .25f, false, 512, true},
		{1, false, .5f, true, 512, false},
		{2, true, .75f, true, 2048, false},
		{3, true, 1.f, true, 4096, false},
	};

	void SetFollowingTier(const TCHAR* Name, const FString& Value)
	{
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		if (!Variable) return;
		// A value set above scalability priority is the user's own and stays.
		if ((uint32(Variable->GetFlags()) & uint32(ECVF_SetByMask)) > uint32(ECVF_SetByScalability)) return;
		Variable->Set(*Value, ECVF_SetByScalability);
	}
}

const FSWGSpaceQualityTier& FSWGSpaceQuality::GetTier(int32 Quality)
{
	return Tiers[FMath::Clamp(Quality, 0, MaxTier)];
}

int32 FSWGSpaceQuality::GetQuality()
{
	return FMath::Clamp(CVarSpaceQuality.GetValueOnAnyThread(), 0, MaxTier);
}

void FSWGSpaceQuality::Apply()
{
	const FSWGSpaceQualityTier& Tier = GetTier(GetQuality());
	SetFollowingTier(TEXT("swg.Space.NebulaQuality"), FString::FromInt(Tier.NebulaQuality));
	SetFollowingTier(TEXT("swg.Space.NebulaWisps"), FString::FromInt(Tier.bNebulaWisps));
	SetFollowingTier(TEXT("swg.Space.DustCountScale"), FString::SanitizeFloat(Tier.DustCountScale));
	SetFollowingTier(TEXT("swg.Space.Debris"), FString::FromInt(Tier.bDebris));
	SetFollowingTier(TEXT("swg.Space.PlanetRegionResolution"), FString::FromInt(Tier.RegionResolution));
	SetFollowingTier(TEXT("swg.Space.HudFlat"), FString::FromInt(Tier.bHudFlat));
}

float FSWGSpaceQuality::GetDustCountScale()
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.DustCountScale"));
	return Variable ? FMath::Clamp(Variable->GetFloat(), 0.f, 1.f) : 1.f;
}

int32 FSWGSpaceQuality::ScaleDustCount(int32 RetailCount)
{
	if (RetailCount <= 0) return 0;
	const float Scale = GetDustCountScale();
	return Scale <= 0.f ? 0 : FMath::Max(1, FMath::RoundToInt(RetailCount*Scale));
}

int32 FSWGSpaceQuality::GetRegionResolution()
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.PlanetRegionResolution"));
	return Variable ? FMath::Clamp(Variable->GetInt(), 128, 4096) : GetTier(GetQuality()).RegionResolution;
}
