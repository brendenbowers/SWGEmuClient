#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGPlanetAppearanceReader.h"
#include "TRE/SWGShaderReader.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGPlanetAppearanceTest, "SWG.Space.PlanetAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGPlanetAppearanceTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>();
	USWGTreSubsystem* Tre = NewObject<USWGTreSubsystem>(Game);
	Tre->AddToRoot(); ON_SCOPE_EXIT { Tre->RemoveFromRoot(); };
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives available"), Tre->LoadArchives())) return false;
	FSWGPlanetAppearanceData Planet;
	const FString Path = TEXT("appearance/planet_tatooine.pln");
	if (!TestTrue(TEXT("Tatooine PLNT parses"), FSWGPlanetAppearanceReader::Read(Tre->CreateIffReader(Path), Planet))) return false;
	TestEqual(TEXT("planet latitude tessellation"), Planet.LatitudeSegments, 64);
	TestEqual(TEXT("planet longitude tessellation"), Planet.LongitudeSegments, 32);
	TestEqual(TEXT("surface radius"), Planet.Surface.Radius, 390.f);
	TestTrue(TEXT("surface spin is degrees per second"), FMath::IsNearlyEqual(Planet.Surface.SpinDegreesPerSecond, 0.005f));
	TestTrue(TEXT("surface UV repeats"), Planet.Surface.UVRepeat.Equals(FVector2f(80,80)));
	TestTrue(TEXT("detail UV repeats"), Planet.Surface.DetailUVRepeat.Equals(FVector2f(10,10)));
	TestEqual(TEXT("cloud radius"), Planet.Clouds.Radius, 391.f);
	TestTrue(TEXT("cloud independent spin"), FMath::IsNearlyEqual(Planet.Clouds.SpinDegreesPerSecond, 0.15f));
	TestTrue(TEXT("halo radius ratio"), FMath::IsNearlyEqual(Planet.HaloRadiusRatio, 1.33f));
	FSWGShaderData Shader;
	TestTrue(TEXT("planet shader parses"), FSWGShaderReader::ReadShader(Tre->CreateIffReader(Planet.Surface.Shader), Shader));
	for (const FString Tag : { FString(TEXT("DTLA")), FString(TEXT("DTLB")), FString(TEXT("MASK")) })
		TestTrue(FString::Printf(TEXT("planet shader retains %s texture"), *Tag), Shader.Textures.ContainsByPredicate([&](const FSWGShaderTexture& Texture) { return Texture.Tag == Tag; }));
	FSWGPlanetAppearanceData Moon;
	TestTrue(TEXT("first moon parses"), FSWGPlanetAppearanceReader::Read(Tre->CreateIffReader(TEXT("appearance/planet_tatooine_moon.pln")), Moon));
	TestEqual(TEXT("first moon radius"), Moon.Surface.Radius, 45.f);
	TestTrue(TEXT("moon has no cloud layer"), Moon.Clouds.Shader.IsEmpty());
	TestTrue(TEXT("second moon parses"), FSWGPlanetAppearanceReader::Read(Tre->CreateIffReader(TEXT("appearance/planet_tatooine_moon_02.pln")), Moon));
	TestEqual(TEXT("second moon radius"), Moon.Surface.Radius, 16.f);
	TArray<uint8> Invalid = Tre->ExtractFile(Path);
	FSWGIffReader Reader(Invalid);
	FSWGIffChunk Version, Init, Surface;
	if (!TestTrue(TEXT("fixture chunks available"), Reader.FindForm(SWG_IFF_TAG('0','0','0','0'), Version)
		&& Reader.FindChildChunk(Version, SWG_IFF_TAG('I','N','I','T'), Init)
		&& Reader.FindChildChunk(Version, SWG_IFF_TAG('S','U','R','F'), Surface))) return false;
	const int32 HugeSegments = MAX_int32;
	FMemory::Memcpy(Invalid.GetData()+Init.DataOffset, &HugeSegments, sizeof(HugeSegments));
	TestFalse(TEXT("unbounded geometry allocation rejected"), FSWGPlanetAppearanceReader::Read(FSWGIffReader(Invalid), Planet));
	TestEqual(TEXT("failed tessellation parse preserves state"), Planet.LatitudeSegments, 64);
	Invalid = Tre->ExtractFile(Path);
	const int32 RadiusOffset = Surface.DataOffset + sizeof(float) + Planet.Surface.Shader.Len() + 1;
	const uint32 NaN = 0x7fc00000;
	FMemory::Memcpy(Invalid.GetData()+RadiusOffset, &NaN, sizeof(NaN));
	TestFalse(TEXT("nonfinite radius rejected"), FSWGPlanetAppearanceReader::Read(FSWGIffReader(Invalid), Planet));
	TestEqual(TEXT("invalid radius preserves state"), Planet.Surface.Radius, 390.f);
	Invalid = Tre->ExtractFile(Path); Invalid[Version.DataOffset-4] = '9';
	TestFalse(TEXT("unsupported version rejected"), FSWGPlanetAppearanceReader::Read(FSWGIffReader(Invalid), Planet));
	return true;
}
#endif
