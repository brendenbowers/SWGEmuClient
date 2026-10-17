#include "Misc/AutomationTest.h"

#include "SWGAudioSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGAudioSettingsRoundTripTest,
	"SWGEmu.Audio.Settings.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSWGAudioSettingsRoundTripTest::RunTest(const FString& Parameters)
{
	USWGAudioSettings* Settings = USWGAudioSettings::Get();

	// The CDO is the real config object, so keep the user's values and put them back at the end.
	TArray<float> Original;
	for (int32 BusIndex = 0; BusIndex < static_cast<int32>(ESWGAudioBus::Count); ++BusIndex)
	{
		Original.Add(Settings->GetBusVolume(static_cast<ESWGAudioBus>(BusIndex)));
	}
	const bool bOriginalMuted = Settings->bMuted;

	Settings->ResetToDefaults();
	TestEqual(TEXT("reset music default"), Settings->GetBusVolume(ESWGAudioBus::Music), USWGAudioSettings::DefaultMusicVolume);
	TestEqual(TEXT("reset master default"), Settings->GetBusVolume(ESWGAudioBus::Master), 1.f);
	TestFalse(TEXT("reset unmutes"), Settings->bMuted);

	for (int32 BusIndex = 0; BusIndex < static_cast<int32>(ESWGAudioBus::Count); ++BusIndex)
	{
		Settings->SetBusVolume(static_cast<ESWGAudioBus>(BusIndex), 0.1f + 0.1f * BusIndex);
	}
	Settings->bMuted = true;
	Settings->Save();

	// Scramble the in-memory values, then read the file back.
	Settings->ResetToDefaults();
	Settings->LoadConfig();
	for (int32 BusIndex = 0; BusIndex < static_cast<int32>(ESWGAudioBus::Count); ++BusIndex)
	{
		TestEqual(FString::Printf(TEXT("bus %d persisted"), BusIndex),
			Settings->GetBusVolume(static_cast<ESWGAudioBus>(BusIndex)), 0.1f + 0.1f * BusIndex, 0.001f);
	}
	TestTrue(TEXT("mute persisted"), Settings->bMuted);

	for (int32 BusIndex = 0; BusIndex < Original.Num(); ++BusIndex)
	{
		Settings->SetBusVolume(static_cast<ESWGAudioBus>(BusIndex), Original[BusIndex]);
	}
	Settings->bMuted = bOriginalMuted;
	Settings->Save();
	return true;
}

#endif
