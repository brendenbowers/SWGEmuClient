#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "SWGEffectsSubsystem.h"
#include "Subsystems/SWGAmbienceSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGWeatherSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	USWGTreSubsystem* LoadTreSubsystem(FAutomationTestBase& Test)
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
		USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
		if (!Tre)
		{
			Test.AddError(TEXT("could not construct a TRE subsystem"));
			return nullptr;
		}

		Tre->AddToRoot();
		Tre->LoadConfig();
		if (!Tre->LoadArchives())
		{
			Test.AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
			Tre->RemoveFromRoot();
			return nullptr;
		}
		return Tre;
	}

	TArray<FString> FindEnvironmentTablePlanets(const USWGTreSubsystem& Tre)
	{
		TArray<FString> Planets;
		for (const FString& Path : Tre.FindVirtualPaths(TEXT("datatables/environment/")))
		{
			if (Path.EndsWith(TEXT(".iff")))
			{
				Planets.AddUnique(FPaths::GetBaseFilename(Path));
			}
		}
		Planets.Sort();
		return Planets;
	}
}

/** Every planet's environment table has the full five weathers per family, and each weather's camera particle exists. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGWeatherTablesTest,
	"SWGEmu.Fx.Weather.Tables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGWeatherTablesTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreSubsystem(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	int32 Tables = 0;
	int32 Families = 0;
	int32 FullFamilies = 0;
	int32 ParticleReferences = 0;
	for (const FString& Planet : FindEnvironmentTablePlanets(*Tre))
	{
		TArray<FSWGEnvironmentRow> Rows;
		if (!USWGAmbienceSubsystem::LoadEnvironmentTable(*Tre, Planet, Rows))
		{
			AddError(FString::Printf(TEXT("%s environment table does not load"), *Planet));
			continue;
		}
		++Tables;

		TMap<FString, TArray<int32>> WeathersByFamily;
		for (const FSWGEnvironmentRow& Row : Rows)
		{
			WeathersByFamily.FindOrAdd(Row.Family.ToLower()).Add(Row.WeatherIndex);

			if (!Row.CameraParticle.IsEmpty())
			{
				++ParticleReferences;
				TestTrue(*FString::Printf(TEXT("%s %s weather %d camera particle %s is in the TRE"), *Planet, *Row.Family, Row.WeatherIndex, *Row.CameraParticle),
					Tre->FileExists(USWGEffectsSubsystem::NormalizeEffectPath(Row.CameraParticle)));
			}
		}

		for (TPair<FString, TArray<int32>>& Pair : WeathersByFamily)
		{
			++Families;
			Pair.Value.Sort();
			// Retail: 5 rows per family, except weatherless families (space, tutorial: 1 row) and
			// planet 09's and taanab's, which stop at the heavy storm (4).
			const int32 RowCount = Pair.Value.Num();
			TestTrue(*FString::Printf(TEXT("%s family %s has 1, 4 or 5 weather rows (has %d)"), *Planet, *Pair.Key, RowCount), RowCount == 1 || RowCount == 4 || RowCount == 5);
			if (RowCount == 4)
			{
				AddInfo(FString::Printf(TEXT("%s family %s has only 4 weather rows"), *Planet, *Pair.Key));
			}
			for (int32 WeatherIndex = 0; WeatherIndex < RowCount; ++WeatherIndex)
			{
				TestEqual(*FString::Printf(TEXT("%s family %s weather row %d"), *Planet, *Pair.Key, WeatherIndex), Pair.Value[WeatherIndex], WeatherIndex);
			}
			if (RowCount == 5)
			{
				++FullFamilies;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d environment tables, %d families (%d with all 5 weathers), %d camera particle references"), Tables, Families, FullFamilies, ParticleReferences));
	TestTrue(TEXT("environment tables are found"), Tables >= 20);
	TestTrue(TEXT("most families have all five weathers"), FullFamilies >= 100);
	return true;
}

/** Row selection by (family, weather), and the wind arithmetic. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGWeatherRowSelectionTest,
	"SWGEmu.Fx.Weather.RowSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGWeatherRowSelectionTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreSubsystem(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	TArray<FSWGEnvironmentRow> Tatooine;
	if (TestTrue(TEXT("tatooine table loads"), USWGAmbienceSubsystem::LoadEnvironmentTable(*Tre, TEXT("tatooine"), Tatooine)))
	{
		const float ExpectedScale[] = { 1.f, 5.f, 7.f, 11.f, 15.f };
		const TCHAR* ExpectedParticle[] = { TEXT(""), TEXT("appearance/pt_dust_storm_light.prt"), TEXT("appearance/pt_dust_storm.prt"), TEXT("appearance/pt_dust_storm_heavy.prt"), TEXT("appearance/pt_dust_storm_heavy.prt") };
		for (int32 WeatherIndex = 0; WeatherIndex < 5; ++WeatherIndex)
		{
			const FSWGEnvironmentRow* Row = USWGAmbienceSubsystem::FindEnvironmentRow(Tatooine, TEXT("global"), WeatherIndex);
			if (TestNotNull(*FString::Printf(TEXT("tatooine global weather %d"), WeatherIndex), Row))
			{
				TestEqual(*FString::Printf(TEXT("weather %d family"), WeatherIndex), Row->Family, FString(TEXT("global")));
				TestEqual(*FString::Printf(TEXT("weather %d index"), WeatherIndex), Row->WeatherIndex, WeatherIndex);
				TestEqual(*FString::Printf(TEXT("weather %d wind scale"), WeatherIndex), Row->WindSpeedScale, ExpectedScale[WeatherIndex]);
				TestEqual(*FString::Printf(TEXT("weather %d camera particle"), WeatherIndex), Row->CameraParticle, FString(ExpectedParticle[WeatherIndex]));
			}
		}

		// Clear weather has clouds, the storm rows replace them with an overcast sky; fog rises with the storm.
		const FSWGEnvironmentRow* Clear = USWGAmbienceSubsystem::FindEnvironmentRow(Tatooine, TEXT("global"), 0);
		const FSWGEnvironmentRow* Storm = USWGAmbienceSubsystem::FindEnvironmentRow(Tatooine, TEXT("global"), 3);
		if (Clear && Storm)
		{
			TestFalse(TEXT("clear weather has clouds"), Clear->CloudTopShader.IsEmpty() && Clear->CloudBottomShader.IsEmpty());
			TestNotEqual(TEXT("storm swaps the gradient sky"), Storm->GradientSky, Clear->GradientSky);
			TestTrue(TEXT("a storm is at least as foggy as clear weather"), Storm->GetFogDensity() >= Clear->GetFogDensity());
		}

		// A request past a family's last weather row uses its worst one (09's families stop at 3).
		TArray<FSWGEnvironmentRow> Short;
		for (int32 WeatherIndex = 0; WeatherIndex < 4; ++WeatherIndex)
		{
			FSWGEnvironmentRow& ShortRow = Short.AddDefaulted_GetRef();
			ShortRow.Family = TEXT("hill");
			ShortRow.WeatherIndex = WeatherIndex;
		}
		const FSWGEnvironmentRow* Worst = USWGAmbienceSubsystem::FindEnvironmentRow(Short, TEXT("hill"), 4);
		if (TestNotNull(TEXT("weather 4 on a four-row family"), Worst))
		{
			TestEqual(TEXT("uses weather 3"), Worst->WeatherIndex, 3);
		}

		const FSWGEnvironmentRow* Unknown =USWGAmbienceSubsystem::FindEnvironmentRow(Tatooine, TEXT("no_such_family"), 2);
		if (TestNotNull(TEXT("an unknown family falls back"), Unknown))
		{
			TestEqual(TEXT("to global"), Unknown->Family, FString(TEXT("global")));
			TestEqual(TEXT("keeping the weather"), Unknown->WeatherIndex, 2);
		}
	}

	// Naboo's storms are rain sheets.
	TArray<FSWGEnvironmentRow> Naboo;
	if (TestTrue(TEXT("naboo table loads"), USWGAmbienceSubsystem::LoadEnvironmentTable(*Tre, TEXT("naboo"), Naboo)))
	{
		bool bFoundRain = false;
		for (const FSWGEnvironmentRow& Row : Naboo)
		{
			bFoundRain |= Row.CameraParticle.Contains(TEXT("pt_rain_sheet"));
		}
		TestTrue(TEXT("naboo has rain sheet weather"), bFoundRain);
	}

	// Wind: the horizontal wire vector times the row's scale; the vertical component is dropped.
	const FVector Speed = USWGWeatherSubsystem::ComputeWindSpeed(FVector(0.5, 0.9, -0.5), 5.f);
	TestEqual(TEXT("wind speed east"), static_cast<float>(Speed.X), 2.5f);
	TestEqual(TEXT("wind speed up is dropped"), static_cast<float>(Speed.Y), 0.f);
	TestEqual(TEXT("wind speed north"), static_cast<float>(Speed.Z), -2.5f);
	TestEqual(TEXT("calm wind is no strength"), USWGWeatherSubsystem::ComputeWindStrength(FVector::ZeroVector), 0.f);
	TestEqual(TEXT("15 m/s is full strength"), USWGWeatherSubsystem::ComputeWindStrength(FVector(15.0, 0.0, 0.0)), 1.f);
	TestEqual(TEXT("strength is clamped"), USWGWeatherSubsystem::ComputeWindStrength(FVector(30.0, 0.0, 30.0)), 1.f);
	TestEqual(TEXT("7.5 m/s is half strength"), USWGWeatherSubsystem::ComputeWindStrength(FVector(0.0, 0.0, -7.5)), 0.5f);
	return true;
}

#endif
