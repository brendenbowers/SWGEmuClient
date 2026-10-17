#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Structure/SWGPlacementRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGPlacementRulesTest, "SWGGame.StructurePlacement.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGPlacementRulesTest::RunTest(const FString& Parameters)
{
	FSWGStructureFootprint House;
	House.Cols = 3; House.Rows = 4;
	House.CenterCol = 1; House.CenterRow = 1;
	const FBox2D Expected[] = {
		FBox2D(FVector2D(-12, -12), FVector2D(12, 20)),
		FBox2D(FVector2D(-12, -12), FVector2D(20, 12)),
		FBox2D(FVector2D(-12, -20), FVector2D(12, 12)),
		FBox2D(FVector2D(-20, -12), FVector2D(12, 12))
	};
	for (int32 Angle = 0; Angle < 4; ++Angle)
	{
		const FBox2D Actual = FSWGPlacementRules::GetServerFootprintRect(House, Angle * 90);
		TestTrue(FString::Printf(TEXT("angle %d min"), Angle * 90), Actual.Min.Equals(Expected[Angle].Min, .001));
		TestTrue(FString::Printf(TEXT("angle %d max"), Angle * 90), Actual.Max.Equals(Expected[Angle].Max, .001));
	}
	const FBox2D First(FVector2D(-4, -4), FVector2D(4, 4));
	TestTrue(TEXT("overlapping footprints"), FSWGPlacementRules::RectsConflict(First, FBox2D(FVector2D(3, 3), FVector2D(8, 8))));
	TestFalse(TEXT("separated footprints"), FSWGPlacementRules::RectsConflict(First, FBox2D(FVector2D(5, 5), FVector2D(9, 9))));
	return true;
}

#endif
