#if WITH_DEV_AUTOMATION_TESTS

#include "SWGMapMarkers.h"
#include "Misc/AutomationTest.h"
#include "Network/Messages/Zone/MapLocationMessages.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGMapLocationMarkersTest,
	"SWGEmu.MapLocations.Detail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGMapLocationMarkersTest::RunTest(const FString& Parameters)
{
	TArray<FSWGMapLocation> Locations;
	Locations.Add({ 1, TEXT("City Cantina"), FVector2D(10.f, 0.f), 3, 0, 0 });
	Locations.Add({ 2, TEXT("Mission Terminal"), FVector2D(20.f, 0.f), 41, 44, 0 });
	Locations.Add({ 3, TEXT("Far Hospital"), FVector2D(200.f, 0.f), 13, 0, 0 });
	TestEqual(TEXT("overview hides locations"), SWGMapMarkers::MakeLocationMarkers(Locations, FVector2D::ZeroVector, 100.f, 0).Num(), 0);
	TestEqual(TEXT("city view shows services"), SWGMapMarkers::MakeLocationMarkers(Locations, FVector2D::ZeroVector, 100.f, 1).Num(), 1);
	TestEqual(TEXT("close view adds terminals"), SWGMapMarkers::MakeLocationMarkers(Locations, FVector2D::ZeroVector, 100.f, 2).Num(), 2);

	TArray<FSWGMapLocation> Buildings;
	Buildings.Add({ 4, TEXT("Mos Eisley"), FVector2D::ZeroVector, 13, 0, 0 });
	Buildings.Add({ 5, TEXT(""), FVector2D::ZeroVector, 7, 8, 0 });
	Buildings.Add({ 6, TEXT("Trainer"), FVector2D::ZeroVector, 19, 20, 0 });
	const TArray<FSWGMapMarker> Markers = SWGMapMarkers::MakeLocationMarkers(Buildings, FVector2D::ZeroVector, 100.f, 1);
	TestEqual(TEXT("guild halls shown, trainers not"), Markers.Num(), 2);
	if (Markers.Num() == 2)
	{
		TestEqual(TEXT("medical center label"), Markers[0].Label.ToString(), FString(TEXT("Medical Center: Mos Eisley")));
		TestEqual(TEXT("unnamed guild hall"), Markers[1].Label.ToString(), FString(TEXT("Guild Hall")));
	}
	return true;
}

#endif
