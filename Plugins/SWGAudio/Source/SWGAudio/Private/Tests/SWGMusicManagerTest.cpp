#include "Misc/AutomationTest.h"

#include "SWGMusicManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Device-free backend: sounds "play" until the test ends or stops them. */
	class FFakeMusicBackend : public ISWGMusicBackend
	{
	public:
		virtual int32 Start(const FString& SndPath, float FadeInSeconds) override
		{
			Playing.Add(++LastToken, SndPath);
			Started.Add(SndPath);
			return LastToken;
		}
		virtual bool IsPlaying(int32 Token) const override { return Playing.Contains(Token); }
		virtual void Stop(int32 Token, float FadeOutSeconds) override { Playing.Remove(Token); }

		void FinishAll() { Playing.Reset(); }
		int32 CountPlaying() const { return Playing.Num(); }
		bool IsPathPlaying(const FString& SndPath) const
		{
			for (const TPair<int32, FString>& Pair : Playing)
			{
				if (Pair.Value == SndPath)
				{
					return true;
				}
			}
			return false;
		}

		TMap<int32, FString> Playing;
		TArray<FString> Started;
		int32 LastToken = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGMusicManagerLayerTest,
	"SWGEmu.Audio.Music.Layers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSWGMusicManagerLayerTest::RunTest(const FString& Parameters)
{
	const FString Ambient = TEXT("sound/music_ambient.snd");
	const FString Combat = TEXT("sound/music_combat.snd");
	const FString Sting = TEXT("sound/music_sting.snd");

	TSharedRef<FFakeMusicBackend> Backend = MakeShared<FFakeMusicBackend>();
	USWGMusicManager* Music = NewObject<USWGMusicManager>(GetTransientPackage());
	Music->SetBackend(Backend);

	Music->SetLayer(ESWGMusicLayer::Environment, Ambient);
	TestTrue(TEXT("environment plays alone"), Backend->IsPathPlaying(Ambient) && Backend->CountPlaying() == 1);

	Music->SetLayer(ESWGMusicLayer::Combat, Combat);
	TestTrue(TEXT("combat masks environment"), Backend->IsPathPlaying(Combat) && !Backend->IsPathPlaying(Ambient) && Backend->CountPlaying() == 1);
	TestTrue(TEXT("combat is audible"), Music->GetAudibleLayer() == ESWGMusicLayer::Combat);

	Music->SetLayer(ESWGMusicLayer::Environment, TEXT("sound/music_other.snd"));
	TestFalse(TEXT("a masked layer does not start"), Backend->IsPathPlaying(TEXT("sound/music_other.snd")));
	Music->SetLayer(ESWGMusicLayer::Environment, Ambient);

	Music->SetLayer(ESWGMusicLayer::Server, Sting);
	TestTrue(TEXT("server masks combat"), Backend->IsPathPlaying(Sting) && Backend->CountPlaying() == 1);

	// The sting finishes by itself: combat resumes.
	Backend->FinishAll();
	Music->Tick(0.1f);
	TestTrue(TEXT("server layer emptied after the sting"), Music->GetLayerPath(ESWGMusicLayer::Server).IsEmpty());
	TestTrue(TEXT("combat resumes"), Backend->IsPathPlaying(Combat) && Backend->CountPlaying() == 1);

	// A looping server track is replaced by silence, which clears it.
	Music->SetLayer(ESWGMusicLayer::Server, TEXT("sound/music_jukebox.snd"));
	TestTrue(TEXT("jukebox plays"), Backend->IsPathPlaying(TEXT("sound/music_jukebox.snd")));
	Music->SetLayer(ESWGMusicLayer::Server, TEXT("sound/music_silence.snd"));
	TestTrue(TEXT("silence clears the server layer"), Music->GetLayerPath(ESWGMusicLayer::Server).IsEmpty());
	TestTrue(TEXT("combat audible again"), Backend->IsPathPlaying(Combat) && Backend->CountPlaying() == 1);

	// Clearing every layer stops everything; setting the same path twice does not restart.
	const int32 StartedBefore = Backend->Started.Num();
	Music->SetLayer(ESWGMusicLayer::Combat, Combat);
	TestEqual(TEXT("same path is a no-op"), Backend->Started.Num(), StartedBefore);
	Music->SetLayer(ESWGMusicLayer::Combat, TEXT("none"));
	Music->SetLayer(ESWGMusicLayer::Environment, FString());
	TestEqual(TEXT("everything stopped"), Backend->CountPlaying(), 0);
	TestTrue(TEXT("nothing audible"), Music->GetAudibleLayer() == ESWGMusicLayer::Count);

	TestTrue(TEXT("silence detection"), USWGMusicManager::IsSilence(TEXT("sound/music_silence.snd")) && USWGMusicManager::IsSilence(TEXT("")) && !USWGMusicManager::IsSilence(Ambient));
	return true;
}

#endif
