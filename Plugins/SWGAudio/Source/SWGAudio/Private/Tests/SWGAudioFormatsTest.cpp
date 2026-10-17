#include "Misc/AutomationTest.h"

#include "Decode/SWGMp3Decoder.h"
#include "Decode/SWGWavDecoder.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGClientEffectReader.h"
#include "TRE/SWGSoundTemplateReader.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Parses every .snd, .cef and sound-bearing .cdf in the mounted TRE set and
 * decodes every sample they reference. Needs the retail data present.
 */
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

	TArray<FString> FindByExtension(const USWGTreSubsystem& Tre, const FString& Extension)
	{
		TArray<FString> Paths = Tre.FindVirtualPaths(Extension);
		Paths.RemoveAll([&Extension](const FString& Path) { return !Path.EndsWith(Extension); });
		Paths.Sort();
		return Paths;
	}

	template<typename TKey>
	FString HistogramToString(const TMap<TKey, int32>& Histogram)
	{
		TArray<TPair<TKey, int32>> Entries;
		for (const TPair<TKey, int32>& Entry : Histogram)
		{
			Entries.Add(Entry);
		}
		Entries.Sort([](const TPair<TKey, int32>& Lhs, const TPair<TKey, int32>& Rhs) { return Lhs.Value > Rhs.Value; });

		FString Result;
		for (const TPair<TKey, int32>& Entry : Entries)
		{
			if constexpr (std::is_same_v<TKey, FString>)
			{
				Result += FString::Printf(TEXT("%s=%d "), *Entry.Key, Entry.Value);
			}
			else
			{
				Result += FString::Printf(TEXT("%d=%d "), static_cast<int32>(Entry.Key), Entry.Value);
			}
		}
		return Result;
	}

	constexpr int32 MaxReportedFailures = 20;

	// Retail .snd files point at a few samples that were never shipped: build-machine absolute
	// paths and the superseded music/music_player_* set. Those are warned about, anything else is an error.
	bool IsKnownDanglingSample(const FString& SamplePath)
	{
		return SamplePath.Contains(TEXT(":/")) || SamplePath.StartsWith(TEXT("music/music_player_"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGAudioFormatsTest,
	"SWGEmu.Audio.Formats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGAudioFormatsTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreSubsystem(*this);
	if (!Tre)
	{
		return false;
	}

	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	TSet<FString> ReferencedSamples;
	int32 ReportedFailures = 0;
	auto ReportFailure = [this, &ReportedFailures](const FString& Message)
	{
		if (ReportedFailures++ < MaxReportedFailures)
		{
			AddError(Message);
		}
	};

	// ── .snd ──────────────────────────────────────────────────────────────
	const TArray<FString> SoundPaths = FindByExtension(*Tre, TEXT(".snd"));
	AddInfo(FString::Printf(TEXT(".snd files: %d (expected 4742)"), SoundPaths.Num()));
	TestTrue(TEXT("a substantial number of .snd files"), SoundPaths.Num() > 4000);

	int32 SoundParseFailures = 0;
	int32 MissingSamples = 0;
	int32 KnownDanglingSamples = 0;
	TSet<FString> KnownDanglingPaths;
	int32 SoundCount3D = 0;
	int32 NonEmpty3DChunk = 0;
	int32 NoSamples = 0;
	TMap<int32, int32> CategoryHistogram, VolumeModeHistogram, PitchModeHistogram, FadeInModeHistogram, FadeOutModeHistogram;
	TMap<int32, int32> LoopDelayModeHistogram, UnknownFlag11Histogram, Unknown12Histogram, Unknown26Histogram;
	TMap<FString, int32> LoopKindHistogram;
	int32 InvertedRanges = 0;

	for (const FString& SoundPath : SoundPaths)
	{
		FSWGSoundTemplate Template;
		if (!FSWGSoundTemplateReader::Read(Tre->CreateIffReader(SoundPath), Template))
		{
			++SoundParseFailures;
			ReportFailure(FString::Printf(TEXT("%s failed to parse"), *SoundPath));
			continue;
		}

		SoundCount3D += Template.b3D ? 1 : 0;
		NonEmpty3DChunk += (Template.b3D && Template.Chunk3DSize != 0) ? 1 : 0;
		NoSamples += Template.Samples.IsEmpty() ? 1 : 0;
		CategoryHistogram.FindOrAdd(Template.CategoryValue)++;
		VolumeModeHistogram.FindOrAdd(Template.Volume.Mode)++;
		PitchModeHistogram.FindOrAdd(Template.Pitch.Mode)++;
		FadeInModeHistogram.FindOrAdd(Template.FadeIn.Mode)++;
		FadeOutModeHistogram.FindOrAdd(Template.FadeOut.Mode)++;
		LoopDelayModeHistogram.FindOrAdd(Template.LoopDelay.Mode)++;
		UnknownFlag11Histogram.FindOrAdd(Template.UnknownFlag11)++;
		Unknown12Histogram.FindOrAdd(Template.Unknown12)++;
		Unknown26Histogram.FindOrAdd(Template.Unknown26)++;
		LoopKindHistogram.FindOrAdd(Template.IsInfiniteLoop() ? TEXT("infinite") : (Template.LoopCountMax > 1 ? TEXT("counted") : TEXT("once")))++;

		InvertedRanges += (Template.Volume.Min > Template.Volume.Max || Template.Pitch.Min > Template.Pitch.Max
			|| Template.StartDelay.Min > Template.StartDelay.Max || Template.LoopDelay.Min > Template.LoopDelay.Max) ? 1 : 0;

		for (const FString& SamplePath : Template.Samples)
		{
			ReferencedSamples.Add(SamplePath);
			if (!Tre->FileExists(SamplePath))
			{
				if (IsKnownDanglingSample(SamplePath))
				{
					++KnownDanglingSamples;
					KnownDanglingPaths.Add(SamplePath);
					AddWarning(FString::Printf(TEXT("%s references missing sample %s"), *SoundPath, *SamplePath));
				}
				else
				{
					++MissingSamples;
					ReportFailure(FString::Printf(TEXT("%s references missing sample %s"), *SoundPath, *SamplePath));
				}
			}
		}
	}

	TestEqual(TEXT(".snd parse failures"), SoundParseFailures, 0);
	TestEqual(TEXT(".snd missing samples"), MissingSamples, 0);
	AddInfo(FString::Printf(TEXT(".snd known-dangling sample refs: %d (%d unique)"), KnownDanglingSamples, KnownDanglingPaths.Num()));
	AddInfo(FString::Printf(TEXT(".snd: 3D=%d nonEmpty0001=%d noSamples=%d invertedRanges=%d uniqueSamples=%d"),
		SoundCount3D, NonEmpty3DChunk, NoSamples, InvertedRanges, ReferencedSamples.Num()));
	AddInfo(TEXT("category: ") + HistogramToString(CategoryHistogram));
	AddInfo(TEXT("volume mode: ") + HistogramToString(VolumeModeHistogram) + TEXT("| pitch mode: ") + HistogramToString(PitchModeHistogram));
	AddInfo(TEXT("fadeIn mode: ") + HistogramToString(FadeInModeHistogram) + TEXT("| fadeOut mode: ") + HistogramToString(FadeOutModeHistogram)
		+ TEXT("| loopDelay mode: ") + HistogramToString(LoopDelayModeHistogram));
	AddInfo(TEXT("t11: ") + HistogramToString(UnknownFlag11Histogram) + TEXT("| t12: ") + HistogramToString(Unknown12Histogram)
		+ TEXT("| t26: ") + HistogramToString(Unknown26Histogram));
	AddInfo(TEXT("loops: ") + HistogramToString(LoopKindHistogram));

	// ── .cef ──────────────────────────────────────────────────────────────
	const TArray<FString> EffectPaths = FindByExtension(*Tre, TEXT(".cef"));
	AddInfo(FString::Printf(TEXT(".cef files: %d (expected 816)"), EffectPaths.Num()));
	TestTrue(TEXT("a substantial number of .cef files"), EffectPaths.Num() > 700);

	int32 EffectParseFailures = 0;
	int32 EmptyEffects = 0;
	int32 EffectMissingSounds = 0;
	int32 EffectSoundCount = 0, ParticleCount = 0, LightCount = 0, ShakeCount = 0, FeedbackCount = 0;
	TMap<int32, int32> EffectVersionHistogram, ParticleFlagHistogram;
	TMap<FString, int32> ParticleTintHistogram;

	for (const FString& EffectPath : EffectPaths)
	{
		if (Tre->ExtractFile(EffectPath).IsEmpty())
		{
			++EmptyEffects; // one retail .cef is 0 bytes
			continue;
		}

		FSWGClientEffect Effect;
		if (!FSWGClientEffectReader::Read(Tre->CreateIffReader(EffectPath), Effect))
		{
			++EffectParseFailures;
			ReportFailure(FString::Printf(TEXT("%s failed to parse"), *EffectPath));
			continue;
		}

		EffectVersionHistogram.FindOrAdd(Effect.Version)++;
		EffectSoundCount += Effect.SoundPaths.Num();
		ParticleCount += Effect.Particles.Num();
		LightCount += Effect.Lights.Num();
		ShakeCount += Effect.CameraShakes.Num();
		FeedbackCount += Effect.ForceFeedbacks.Num();

		for (const FSWGClientEffectParticle& Particle : Effect.Particles)
		{
			if (Effect.Version >= 2)
			{
				ParticleFlagHistogram.FindOrAdd(Particle.Flag)++;
			}
			if (Effect.Version >= 3)
			{
				ParticleTintHistogram.FindOrAdd(FString::Printf(TEXT("%g,%g,%g,%g"), Particle.Tint[0], Particle.Tint[1], Particle.Tint[2], Particle.Tint[3]))++;
			}
		}

		for (const FString& SoundPath : Effect.SoundPaths)
		{
			if (!Tre->FileExists(SoundPath))
			{
				// Two retail e3_* demo effects point at .snd files that were never shipped.
				if (EffectPath.Contains(TEXT("/e3_")))
				{
					AddWarning(FString::Printf(TEXT("%s references missing sound %s"), *EffectPath, *SoundPath));
				}
				else
				{
					++EffectMissingSounds;
					ReportFailure(FString::Printf(TEXT("%s references missing sound %s"), *EffectPath, *SoundPath));
				}
			}
		}
	}

	TestEqual(TEXT(".cef parse failures"), EffectParseFailures, 0);
	TestEqual(TEXT(".cef missing sounds"), EffectMissingSounds, 0);
	AddInfo(FString::Printf(TEXT(".cef: empty=%d sounds=%d particles=%d lights=%d shakes=%d feedback=%d"),
		EmptyEffects, EffectSoundCount, ParticleCount, LightCount, ShakeCount, FeedbackCount));
	AddInfo(TEXT("cef version: ") + HistogramToString(EffectVersionHistogram) + TEXT("| cpap flag: ") + HistogramToString(ParticleFlagHistogram)
		+ TEXT("| cpap tint: ") + HistogramToString(ParticleTintHistogram));

	// ── .cdf sound hooks ──────────────────────────────────────────────────
	// Dangling targets are warnings only: retail clientdata is not guaranteed consistent.
	const TArray<FString> ClientDataPaths = FindByExtension(*Tre, TEXT(".cdf"));
	int32 ClientDataParsed = 0, ClientDataEmpty = 0, ClientDataFailed = 0, ClientDataMissingTargets = 0;
	int32 AttachedSoundCount = 0, SoundEventCount = 0, EffectEventCount = 0, PlayerEventCount = 0;
	TMap<FString, int32> SoundEventNames, EffectEventNames, PlayerEventNames, EffectEventTargetKinds;

	for (const FString& ClientDataPath : ClientDataPaths)
	{
		if (Tre->ExtractFile(ClientDataPath).IsEmpty())
		{
			++ClientDataEmpty;
			continue;
		}

		FSWGClientDataFile ClientData;
		if (!FSWGClientDataFileReader::Read(Tre->CreateIffReader(ClientDataPath), ClientData))
		{
			++ClientDataFailed;
			ReportFailure(FString::Printf(TEXT("%s failed to parse"), *ClientDataPath));
			continue;
		}
		++ClientDataParsed;

		AttachedSoundCount += ClientData.AttachedSounds.Num();
		SoundEventCount += ClientData.SoundEvents.Num();
		EffectEventCount += ClientData.EffectEvents.Num();
		PlayerEventCount += ClientData.PlayerEvents.Num();

		auto CheckTarget = [&](const FString& Target)
		{
			if (!Tre->FileExists(Target))
			{
				if (ClientDataMissingTargets++ < MaxReportedFailures)
				{
					AddWarning(FString::Printf(TEXT("%s references missing %s"), *ClientDataPath, *Target));
				}
			}
		};

		for (const FString& Attached : ClientData.AttachedSounds)
		{
			CheckTarget(Attached);
		}
		for (const FSWGClientDataEvent& Event : ClientData.SoundEvents)
		{
			SoundEventNames.FindOrAdd(Event.EventName)++;
			CheckTarget(Event.Target);
		}
		for (const FSWGClientDataEvent& Event : ClientData.EffectEvents)
		{
			EffectEventNames.FindOrAdd(Event.EventName)++;
			EffectEventTargetKinds.FindOrAdd(FPaths::GetExtension(Event.Target))++;
			CheckTarget(Event.Target);
		}
		for (const FSWGClientDataEvent& Event : ClientData.PlayerEvents)
		{
			PlayerEventNames.FindOrAdd(Event.EventName)++;
			CheckTarget(Event.Target);
		}
	}

	TestEqual(TEXT(".cdf parse failures"), ClientDataFailed, 0);
	AddInfo(FString::Printf(TEXT(".cdf: files=%d parsed=%d empty=%d missingTargets=%d asnd=%d csnd=%d ceft=%d evnt=%d"),
		ClientDataPaths.Num(), ClientDataParsed, ClientDataEmpty, ClientDataMissingTargets,
		AttachedSoundCount, SoundEventCount, EffectEventCount, PlayerEventCount));
	AddInfo(TEXT("csnd events: ") + HistogramToString(SoundEventNames));
	AddInfo(TEXT("ceft events: ") + HistogramToString(EffectEventNames));
	AddInfo(TEXT("ceft target ext: ") + HistogramToString(EffectEventTargetKinds));
	AddInfo(TEXT("evnt events: ") + HistogramToString(PlayerEventNames));

	// ── decode every referenced sample ────────────────────────────────────
	int32 WavDecoded = 0, Mp3Decoded = 0, DecodeFailures = 0, WavWithLoop = 0, BadLoopPoints = 0;
	TMap<int32, int32> SampleRateHistogram, ChannelHistogram;
	double LongestSeconds = 0.0;
	FString LongestPath;

	// Music mp3s are referenced from music.iff rather than a .snd, so add them explicitly.
	ReferencedSamples.Append(FindByExtension(*Tre, TEXT(".mp3")));
	TArray<FString> SampleList = ReferencedSamples.Array();
	SampleList.Sort();
	for (const FString& SamplePath : SampleList)
	{
		if (KnownDanglingPaths.Contains(SamplePath))
		{
			continue;
		}

		const TArray<uint8> Bytes = Tre->ExtractFile(SamplePath);
		FSWGDecodedPcm Pcm;
		FString Error;
		const bool bIsMp3 = SamplePath.EndsWith(TEXT(".mp3"));
		const bool bOk = bIsMp3 ? FSWGMp3Decoder::Decode(Bytes, Pcm, &Error) : FSWGWavDecoder::Decode(Bytes, Pcm, &Error);
		if (!bOk)
		{
			++DecodeFailures;
			ReportFailure(FString::Printf(TEXT("%s failed to decode: %s"), *SamplePath, *Error));
			continue;
		}

		(bIsMp3 ? Mp3Decoded : WavDecoded)++;
		SampleRateHistogram.FindOrAdd(Pcm.SampleRate)++;
		ChannelHistogram.FindOrAdd(Pcm.NumChannels)++;
		if (Pcm.HasLoop())
		{
			++WavWithLoop;
			BadLoopPoints += (Pcm.LoopEndFrame > Pcm.GetNumFrames()) ? 1 : 0;
		}
		if (Pcm.GetDurationSeconds() > LongestSeconds)
		{
			LongestSeconds = Pcm.GetDurationSeconds();
			LongestPath = SamplePath;
		}
	}

	TestEqual(TEXT("sample decode failures"), DecodeFailures, 0);
	TestEqual(TEXT("loop points inside the sample"), BadLoopPoints, 0);
	TestTrue(TEXT("decoded wavs"), WavDecoded > 1000);
	TestTrue(TEXT("decoded mp3s"), Mp3Decoded > 0);
	AddInfo(FString::Printf(TEXT("decoded: wav=%d mp3=%d withLoop=%d longest=%s (%.1fs)"), WavDecoded, Mp3Decoded, WavWithLoop, *LongestPath, LongestSeconds));
	AddInfo(TEXT("sample rates: ") + HistogramToString(SampleRateHistogram) + TEXT("| channels: ") + HistogramToString(ChannelHistogram));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
