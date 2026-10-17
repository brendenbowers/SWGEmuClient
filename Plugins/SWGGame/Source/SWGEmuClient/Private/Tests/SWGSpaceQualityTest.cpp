#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "Common/SWGSpaceQuality.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	const TCHAR* const QualityName = TEXT("swg.Space.Quality");
	const TCHAR* const FeatureNames[] = {TEXT("swg.Space.NebulaQuality"), TEXT("swg.Space.NebulaWisps"), TEXT("swg.Space.DustCountScale"),
		TEXT("swg.Space.Debris"), TEXT("swg.Space.PlanetRegionResolution"), TEXT("swg.Space.HudFlat")};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceQualityTest, "SWG.Space.Quality", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpaceQualityTest::RunTest(const FString& Parameters)
{
	IConsoleManager& Console = IConsoleManager::Get();
	IConsoleVariable* Quality = Console.FindConsoleVariable(QualityName);
	if (!TestNotNull(TEXT("swg.Space.Quality exists"), Quality)) return false;
	TestEqual(TEXT("the default is the top tier"), Quality->GetInt(), 3);

	// Other tests set these with ECVF_SetByCode; clear that history so the tiers are free to drive them, and put it all back afterwards.
	struct FSaved { IConsoleVariable* Variable; FString Value; uint32 SetBy; };
	TArray<FSaved> Saved;
	for (const TCHAR* Name : FeatureNames)
		if (IConsoleVariable* Variable = Console.FindConsoleVariable(Name)) Saved.Add({Variable, Variable->GetString(), uint32(Variable->GetFlags()) & uint32(ECVF_SetByMask)});
	Saved.Add({Quality, Quality->GetString(), uint32(Quality->GetFlags()) & uint32(ECVF_SetByMask)});
	ON_SCOPE_EXIT
	{
		for (const FSaved& Entry : Saved)
		{
			Entry.Variable->ClearFlags(ECVF_SetByMask);
			Entry.Variable->Set(*Entry.Value, EConsoleVariableFlags(Entry.SetBy));
		}
	};
	auto FollowTier = [&]() { for (const FSaved& Entry : Saved) Entry.Variable->ClearFlags(ECVF_SetByMask); };
	auto Find = [&](const TCHAR* Name) { return Console.FindConsoleVariable(Name); };
	for (const TCHAR* Name : FeatureNames)
		if (!TestNotNull(*FString::Printf(TEXT("%s exists"), Name), Find(Name))) return false;

	// Expected rows, written out so the table in the plan is checked, not just echoed.
	struct FExpected { int32 Nebula; bool bWisps; float DustScale; bool bDebris; int32 Region; bool bHudFlat; int32 DustOfRetail750; };
	const FExpected Expected[4] = {{0, false, .25f, false, 512, true, 188}, {1, false, .5f, true, 512, false, 375}, {2, true, .75f, true, 2048, false, 563}, {3, true, 1.f, true, 4096, false, 750}};
	FollowTier();
	for (int32 Tier = 0; Tier < 4; ++Tier)
	{
		Quality->Set(Tier, ECVF_SetByCode);
		const FString Label = FString::Printf(TEXT("tier %d"), Tier);
		const FExpected& Row = Expected[Tier];
		TestEqual(*(Label + TEXT(" nebula quality")), Find(FeatureNames[0])->GetInt(), Row.Nebula);
		TestTrue(*(Label + TEXT(" nebula wisps")), Find(FeatureNames[1])->GetBool() == Row.bWisps);
		TestEqual(*(Label + TEXT(" dust count scale")), Find(FeatureNames[2])->GetFloat(), Row.DustScale);
		TestTrue(*(Label + TEXT(" debris")), Find(FeatureNames[3])->GetBool() == Row.bDebris);
		TestEqual(*(Label + TEXT(" region resolution")), Find(FeatureNames[4])->GetInt(), Row.Region);
		TestTrue(*(Label + TEXT(" HUD flat")), Find(FeatureNames[5])->GetBool() == Row.bHudFlat);
		TestEqual(*(Label + TEXT(" dust instances of the retail 750")), FSWGSpaceQuality::ScaleDustCount(750), Row.DustOfRetail750);
		TestEqual(*(Label + TEXT(" region resolution accessor")), FSWGSpaceQuality::GetRegionResolution(), Row.Region);
		const FSWGSpaceQualityTier& Table = FSWGSpaceQuality::GetTier(Tier);
		TestTrue(*(Label + TEXT(" table row matches")), Table.NebulaQuality == Row.Nebula && Table.bNebulaWisps == Row.bWisps && Table.DustCountScale == Row.DustScale
			&& Table.bDebris == Row.bDebris && Table.RegionResolution == Row.Region && Table.bHudFlat == Row.bHudFlat);
	}
	TestEqual(TEXT("the top tier keeps the retail dust count"), FSWGSpaceQuality::ScaleDustCount(750), 750);
	TestEqual(TEXT("a zone with no dust stays without"), FSWGSpaceQuality::ScaleDustCount(0), 0);
	TestEqual(TEXT("tiers out of range clamp"), FSWGSpaceQuality::GetTier(9).NebulaQuality, 3);
	Quality->Set(-2, ECVF_SetByCode);
	TestEqual(TEXT("a negative quality reads as tier 0"), FSWGSpaceQuality::GetQuality(), 0);

	// An explicit per-feature value wins over every later tier change; the other features keep following.
	FollowTier();
	Quality->Set(3, ECVF_SetByCode);
	Find(FeatureNames[0])->Set(1, ECVF_SetByCode);
	Find(FeatureNames[2])->Set(.4f, ECVF_SetByConsole);
	for (const int32 Tier : {0, 2, 3})
	{
		Quality->Set(Tier, ECVF_SetByCode);
		TestEqual(*FString::Printf(TEXT("tier %d keeps the explicit nebula quality"), Tier), Find(FeatureNames[0])->GetInt(), 1);
		TestEqual(*FString::Printf(TEXT("tier %d keeps the explicit dust scale"), Tier), Find(FeatureNames[2])->GetFloat(), .4f);
		TestEqual(*FString::Printf(TEXT("tier %d still drives the region resolution"), Tier), Find(FeatureNames[4])->GetInt(), Expected[Tier].Region);
		TestTrue(*FString::Printf(TEXT("tier %d still drives debris"), Tier), Find(FeatureNames[3])->GetBool() == Expected[Tier].bDebris);
	}
	TestEqual(TEXT("the explicit dust scale feeds the instance count"), FSWGSpaceQuality::ScaleDustCount(750), 300);
	return true;
}
#endif
