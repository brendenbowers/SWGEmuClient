#include "Misc/AutomationTest.h"

#include "Decode/SWGMp3Decoder.h"
#include "Decode/SWGWavDecoder.h"
#include "Engine/GameInstance.h"
#include "Playback/SWGPcmStreamSource.h"
#include "Playback/SWGSoundScheduler.h"
#include "SWGAudioTypes.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGSoundTemplateReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr float StepSeconds = 0.05f;

	FSWGSoundRandomRange MakeRange(int32 Mode, float Min, float Max)
	{
		FSWGSoundRandomRange Range;
		Range.Mode = Mode;
		Range.Min = Min;
		Range.Max = Max;
		return Range;
	}

	FSWGSoundTemplate MakeTemplate(int32 SampleCount, int32 LoopMin, int32 LoopMax)
	{
		FSWGSoundTemplate Template;
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			Template.Samples.Add(FString::Printf(TEXT("sample/test_%d.wav"), SampleIndex));
		}
		Template.LoopCountMin = LoopMin;
		Template.LoopCountMax = LoopMax;
		Template.Volume = MakeRange(0, 1.f, 1.f);
		return Template;
	}

	/** Simulated driver: runs the scheduler over fixed-length iterations and records when each one starts. */
	struct FSimulatedRun
	{
		TArray<float> StartTimes;
		TArray<float> EndTimes;
		TArray<FSWGSoundIteration> Iterations;
		bool bFinished = false;
	};

	FSimulatedRun Simulate(const FSWGSoundTemplate& Template, int32 Seed, float IterationSeconds, float MaxSeconds, int32 MaxIterations)
	{
		FSimulatedRun Run;
		FSWGSoundScheduler Scheduler;
		Scheduler.Begin(Template, Seed);

		float Now = 0.f;
		float IterationEnd = -1.f;
		while (Now < MaxSeconds && Run.StartTimes.Num() < MaxIterations)
		{
			Now += StepSeconds;
			if (IterationEnd >= 0.f && Now >= IterationEnd)
			{
				Run.EndTimes.Add(IterationEnd);
				IterationEnd = -1.f;
				Scheduler.NotifyIterationFinished();
			}

			FSWGSoundIteration Iteration;
			if (Scheduler.Tick(StepSeconds, Iteration))
			{
				Run.StartTimes.Add(Now);
				Run.Iterations.Add(Iteration);
				IterationEnd = Now + IterationSeconds;
			}
			if (Scheduler.IsFinished())
			{
				break;
			}
		}
		Run.bFinished = Scheduler.IsFinished();
		return Run;
	}

	TSharedPtr<FSWGDecodedPcm> MakeRampPcm(int32 Frames, int32 Channels = 1)
	{
		TSharedPtr<FSWGDecodedPcm> Pcm = MakeShared<FSWGDecodedPcm>();
		Pcm->NumChannels = Channels;
		Pcm->SampleRate = 1000;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			for (int32 Channel = 0; Channel < Channels; ++Channel)
			{
				Pcm->Samples.Add(static_cast<int16>(1000 + Frame));
			}
		}
		return Pcm;
	}

	USWGTreSubsystem* LoadTreSubsystem()
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
		USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
		if (!Tre)
		{
			return nullptr;
		}
		Tre->AddToRoot();
		Tre->LoadConfig();
		if (!Tre->LoadArchives())
		{
			Tre->RemoveFromRoot();
			return nullptr;
		}
		return Tre;
	}

	bool ReadTemplate(USWGTreSubsystem& Tre, const FString& Path, FSWGSoundTemplate& OutTemplate)
	{
		return FSWGSoundTemplateReader::Read(Tre.CreateIffReader(Path), OutTemplate);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGAudioPlaybackTest,
	"SWGEmu.Audio.Playback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGAudioPlaybackTest::RunTest(const FString& Parameters)
{
	// ── Scheduler: loop count ─────────────────────────────────────────────
	{
		const FSimulatedRun Once = Simulate(MakeTemplate(1, 1, 1), 1, 1.f, 30.f, 100);
		TestEqual(TEXT("one-shot runs once"), Once.StartTimes.Num(), 1);
		TestTrue(TEXT("one-shot finishes"), Once.bFinished);

		const FSimulatedRun Counted = Simulate(MakeTemplate(1, 3, 3), 1, 1.f, 60.f, 100);
		TestEqual(TEXT("fixed loop count"), Counted.StartTimes.Num(), 3);
		TestTrue(TEXT("counted loop finishes"), Counted.bFinished);

		int32 SeenMin = INT_MAX, SeenMax = 0;
		for (int32 Seed = 0; Seed < 60; ++Seed)
		{
			const int32 Count = Simulate(MakeTemplate(1, 2, 4), Seed, 0.5f, 60.f, 100).StartTimes.Num();
			SeenMin = FMath::Min(SeenMin, Count);
			SeenMax = FMath::Max(SeenMax, Count);
		}
		TestTrue(TEXT("ranged loop count stays in [2,4]"), SeenMin >= 2 && SeenMax <= 4);
		TestTrue(TEXT("ranged loop count actually varies"), SeenMax > SeenMin);

		const FSimulatedRun Infinite = Simulate(MakeTemplate(1, -1, -1), 1, 0.5f, 600.f, 50);
		TestEqual(TEXT("infinite loop keeps going"), Infinite.StartTimes.Num(), 50);
		TestFalse(TEXT("infinite loop never finishes"), Infinite.bFinished);

		FSWGSoundTemplate Empty = MakeTemplate(0, 1, 1);
		FSWGSoundScheduler EmptyScheduler;
		EmptyScheduler.Begin(Empty, 1);
		TestTrue(TEXT("template without samples is finished at once"), EmptyScheduler.IsFinished());
	}

	// ── Scheduler: delays ─────────────────────────────────────────────────
	{
		FSWGSoundTemplate Template = MakeTemplate(1, -1, -1);
		Template.StartDelay = MakeRange(0, 2.f, 5.f);
		Template.LoopDelay = MakeRange(2, 3.f, 6.f);

		bool bStartInRange = true, bGapInRange = true;
		float ShortestGap = FLT_MAX, LongestGap = 0.f;
		for (int32 Seed = 0; Seed < 40; ++Seed)
		{
			const FSimulatedRun Run = Simulate(Template, Seed, 1.f, 600.f, 6);
			bStartInRange &= Run.StartTimes.Num() > 0 && Run.StartTimes[0] >= 2.f && Run.StartTimes[0] <= 5.f + 2.f * StepSeconds;
			for (int32 Index = 1; Index < Run.StartTimes.Num(); ++Index)
			{
				const float Gap = Run.StartTimes[Index] - Run.EndTimes[Index - 1];
				ShortestGap = FMath::Min(ShortestGap, Gap);
				LongestGap = FMath::Max(LongestGap, Gap);
				bGapInRange &= Gap >= 3.f - StepSeconds && Gap <= 6.f + 2.f * StepSeconds;
			}
		}
		TestTrue(TEXT("start delay within [2,5]"), bStartInRange);
		TestTrue(*FString::Printf(TEXT("loop delay within [3,6] (saw %.2f..%.2f)"), ShortestGap, LongestGap), bGapInRange);
		TestTrue(TEXT("loop delay actually varies"), LongestGap - ShortestGap > 1.f);

		FSWGSoundTemplate NoDelay = MakeTemplate(1, 1, 1);
		FSWGSoundScheduler Scheduler;
		Scheduler.Begin(NoDelay, 1);
		FSWGSoundIteration Iteration;
		TestTrue(TEXT("no start delay starts on the first tick"), Scheduler.Tick(0.016f, Iteration));
	}

	// ── Scheduler: randomised parameters, determinism ─────────────────────
	{
		FSWGSoundTemplate Template = MakeTemplate(3, -1, -1);
		Template.Volume = MakeRange(2, 0.4f, 0.9f);
		Template.Pitch = MakeRange(2, -3.f, 3.f);
		Template.FadeIn = MakeRange(2, 0.1f, 0.3f);
		Template.FadeOut = MakeRange(0, 0.5f, 0.5f);

		const FSimulatedRun RunA = Simulate(Template, 77, 0.2f, 1000.f, 200);
		const FSimulatedRun RunB = Simulate(Template, 77, 0.2f, 1000.f, 200);
		const FSimulatedRun RunC = Simulate(Template, 78, 0.2f, 1000.f, 200);

		bool bInBounds = true, bDeterministic = RunA.Iterations.Num() == RunB.Iterations.Num(), bDiffers = false, bNoRepeat = true;
		for (int32 Index = 0; Index < RunA.Iterations.Num(); ++Index)
		{
			const FSWGSoundIteration& Iteration = RunA.Iterations[Index];
			bInBounds &= Iteration.Volume >= 0.4f && Iteration.Volume <= 0.9f
				&& Iteration.PitchSemitones >= -3.f && Iteration.PitchSemitones <= 3.f
				&& FMath::IsNearlyEqual(Iteration.PitchMultiplier, FMath::Pow(2.f, Iteration.PitchSemitones / 12.f), 1e-4f)
				&& Iteration.FadeInSeconds >= 0.1f && Iteration.FadeInSeconds <= 0.3f
				&& Iteration.FadeOutSeconds == 0.f // mode 0 means no fade even though the range holds a value
				&& Iteration.SampleIndex >= 0 && Iteration.SampleIndex < 3;
			if (Index > 0)
			{
				bNoRepeat &= Iteration.SampleIndex != RunA.Iterations[Index - 1].SampleIndex;
			}
			if (RunB.Iterations.IsValidIndex(Index))
			{
				bDeterministic &= Iteration.SampleIndex == RunB.Iterations[Index].SampleIndex && Iteration.Volume == RunB.Iterations[Index].Volume
					&& Iteration.PitchSemitones == RunB.Iterations[Index].PitchSemitones;
			}
			if (RunC.Iterations.IsValidIndex(Index) && RunC.Iterations[Index].Volume != Iteration.Volume)
			{
				bDiffers = true;
			}
		}
		TestTrue(TEXT("volume, pitch, fades and sample index stay in bounds"), bInBounds);
		TestTrue(TEXT("same seed gives the same sequence"), bDeterministic);
		TestTrue(TEXT("different seed gives a different sequence"), bDiffers);
		TestTrue(TEXT("a multi-sample sound never repeats a variation back to back"), bNoRepeat);
	}

	// ── Scheduler: slow drift (mode 3) ────────────────────────────────────
	{
		FSWGSoundTemplate Template = MakeTemplate(1, -1, -1);
		Template.Volume = MakeRange(3, 0.5f, 1.f);
		Template.Volume.ExtraA = 2.f;
		Template.Volume.ExtraB = 1.5f;
		Template.Pitch = MakeRange(3, -2.f, 2.f);
		Template.Pitch.ExtraA = 2.f;
		Template.Pitch.ExtraB = 1.5f;
		FSWGSoundScheduler Scheduler;
		Scheduler.Begin(Template, 5);

		bool bInBounds = true;
		float LowestVolume = FLT_MAX, HighestVolume = 0.f, LargestStep = 0.f, Previous = Scheduler.GetVolume();
		FSWGSoundIteration Iteration;
		for (int32 Step = 0; Step < 6000; ++Step)
		{
			Scheduler.Tick(0.016f, Iteration);
			const float Volume = Scheduler.GetVolume();
			bInBounds &= Volume >= 0.5f - KINDA_SMALL_NUMBER && Volume <= 1.f + KINDA_SMALL_NUMBER;
			LowestVolume = FMath::Min(LowestVolume, Volume);
			HighestVolume = FMath::Max(HighestVolume, Volume);
			LargestStep = FMath::Max(LargestStep, FMath::Abs(Volume - Previous));
			Previous = Volume;
		}
		TestTrue(TEXT("drifting volume stays inside its range"), bInBounds);
		TestTrue(TEXT("drifting volume actually moves"), HighestVolume - LowestVolume > 0.1f);
		TestTrue(TEXT("drifting volume moves smoothly"), LargestStep < 0.01f);
	}

	// ── Scheduler: retail mode semantics ──────────────────────────────────
	{
		FSWGSoundTemplate Template = MakeTemplate(1, 3, 3);
		Template.Volume = MakeRange(0, 0.2f, 0.4f);
		Template.Pitch = MakeRange(0, 5.f, 5.f);
		const FSimulatedRun Fixed = Simulate(Template, 3, 0.5f, 60.f, 10);
		TestTrue(TEXT("volume mode 0 plays at full volume"), Fixed.Iterations.Num() == 3 && Fixed.Iterations[0].Volume == 1.f);
		TestTrue(TEXT("pitch mode 0 is unshifted"), Fixed.Iterations.Num() == 3 && Fixed.Iterations[0].PitchSemitones == 0.f);

		Template.Volume = MakeRange(1, 0.2f, 0.9f);
		Template.FadeIn = MakeRange(1, 0.5f, 0.5f);
		Template.FadeOut = MakeRange(1, 0.7f, 0.7f);
		const FSimulatedRun Once = Simulate(Template, 3, 0.5f, 60.f, 10);
		bool bVolumeHeld = Once.Iterations.Num() == 3;
		for (const FSWGSoundIteration& Iteration : Once.Iterations)
		{
			bVolumeHeld &= Iteration.Volume == Once.Iterations[0].Volume;
		}
		TestTrue(TEXT("volume mode 1 draws once per sound"), bVolumeHeld);
		TestTrue(TEXT("fade-in mode 1 applies to the first iteration only"),
			Once.Iterations.Num() == 3 && Once.Iterations[0].FadeInSeconds == 0.5f && Once.Iterations[1].FadeInSeconds == 0.f);
		TestTrue(TEXT("fade-out mode 1 applies to the last iteration only"),
			Once.Iterations.Num() == 3 && Once.Iterations[0].FadeOutSeconds == 0.f && Once.Iterations[2].FadeOutSeconds == 0.7f);
	}

	// ── Fade envelope ─────────────────────────────────────────────────────
	{
		TestEqual(TEXT("fade-in start"), FSWGSoundScheduler::ComputeFadeGain(0.f, 4.f, 1.f, 1.f), 0.f);
		TestEqual(TEXT("fade-in halfway"), FSWGSoundScheduler::ComputeFadeGain(0.5f, 4.f, 1.f, 1.f), 0.5f);
		TestEqual(TEXT("full volume between fades"), FSWGSoundScheduler::ComputeFadeGain(2.f, 4.f, 1.f, 1.f), 1.f);
		TestEqual(TEXT("fade-out halfway"), FSWGSoundScheduler::ComputeFadeGain(3.5f, 4.f, 1.f, 1.f), 0.5f);
		TestEqual(TEXT("fade-out end"), FSWGSoundScheduler::ComputeFadeGain(4.f, 4.f, 1.f, 1.f), 0.f);
		TestEqual(TEXT("no fades"), FSWGSoundScheduler::ComputeFadeGain(0.f, 4.f, 0.f, 0.f), 1.f);
	}

	// ── PCM stream source ─────────────────────────────────────────────────
	{
		// One-shot with a 10-frame fade-in and fade-out over 100 frames at 1 kHz.
		FSWGPcmStreamSource::FConfig Config;
		Config.Pcm = MakeRampPcm(100);
		Config.FadeInSeconds = 0.01f;
		Config.FadeOutSeconds = 0.01f;
		FSWGPcmStreamSource Source(Config);

		TArray<int16> Block;
		Block.SetNumZeroed(120);
		Source.Generate(Block.GetData(), 120);
		TestEqual(TEXT("fade-in starts silent"), static_cast<int32>(Block[0]), 0);
		TestEqual(TEXT("fade-in reaches full volume"), static_cast<int32>(Block[50]), 1050);
		TestTrue(TEXT("fade-out ends near silence"), Block[99] < 120);
		TestEqual(TEXT("silence after the end"), static_cast<int32>(Block[110]), 0);
		Source.Generate(Block.GetData(), 4);
		TestTrue(TEXT("one-shot reports exhausted"), Source.IsExhausted());
	}
	{
		// Loop region [10,20) of a 40-frame sample: intro 0..19, then 10..19 repeating.
		TSharedPtr<FSWGDecodedPcm> Pcm = MakeRampPcm(40);
		FSWGPcmStreamSource::FConfig Config;
		Config.Pcm = Pcm;
		Config.bLoop = true;
		Config.LoopStartFrame = 10;
		Config.LoopEndFrame = 20;
		FSWGPcmStreamSource Source(Config);

		TArray<int16> Block;
		Block.SetNumZeroed(40);
		Source.Generate(Block.GetData(), 40);
		bool bMatches = true;
		for (int32 Index = 0; Index < 40; ++Index)
		{
			const int32 ExpectedFrame = Index < 20 ? Index : 10 + ((Index - 20) % 10);
			bMatches &= Block[Index] == 1000 + ExpectedFrame;
		}
		TestTrue(TEXT("loop plays the intro, then repeats [LoopStart, LoopEnd)"), bMatches);
		TestFalse(TEXT("a loop is never exhausted"), Source.IsExhausted());

		Source.RequestStop(0.01f);
		Source.Generate(Block.GetData(), 40);
		TestTrue(TEXT("stop fade exhausts a loop"), Source.IsExhausted());
		TestTrue(TEXT("stop fade ramps down"), Block[0] > Block[8] || Block[8] == 0);
	}
	{
		// A wav smpl loop on the decoded sample is honoured when no override is given.
		TSharedPtr<FSWGDecodedPcm> Pcm = MakeRampPcm(30);
		Pcm->LoopStartFrame = 5;
		Pcm->LoopEndFrame = 15;
		FSWGPcmStreamSource::FConfig Config;
		Config.Pcm = Pcm;
		Config.bLoop = true;
		FSWGPcmStreamSource Source(Config);
		TestEqual(TEXT("smpl loop start used"), Source.GetLoopStartFrame(), 5);
		TestEqual(TEXT("smpl loop end used"), Source.GetLoopEndFrame(), 15);

		Pcm->LoopStartFrame = INDEX_NONE;
		Pcm->LoopEndFrame = INDEX_NONE;
		FSWGPcmStreamSource WholeSource(Config);
		TestEqual(TEXT("no loop points loops the whole sample"), WholeSource.GetLoopEndFrame(), 30);
	}

	// ── Path normalisation ────────────────────────────────────────────────
	{
		TestEqual(TEXT("bare name"), SWGAudio::NormalizeSoundPath(TEXT("ui_rollover")), FString(TEXT("sound/ui_rollover.snd")));
		TestEqual(TEXT("no extension"), SWGAudio::NormalizeSoundPath(TEXT("sound/ui_rollover")), FString(TEXT("sound/ui_rollover.snd")));
		TestEqual(TEXT("full path"), SWGAudio::NormalizeSoundPath(TEXT("Sound\\UI_Rollover.snd")), FString(TEXT("sound/ui_rollover.snd")));
		TestEqual(TEXT("other directory is untouched"), SWGAudio::NormalizeSoundPath(TEXT("music/theme.snd")), FString(TEXT("music/theme.snd")));
		TestEqual(TEXT("cef bare name"), SWGAudio::NormalizeEffectPath(TEXT("door_open")), FString(TEXT("clienteffect/door_open.cef")));
	}

	// ── Real data ─────────────────────────────────────────────────────────
	USWGTreSubsystem* Tre = LoadTreSubsystem();
	if (!Tre)
	{
		AddWarning(TEXT("no .tre archives loaded - skipping the real-data checks"));
		return true;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	FSWGSoundTemplate Drip;
	if (TestTrue(TEXT("amb_cave_drip_rnd.snd parses"), ReadTemplate(*Tre, TEXT("sound/amb_cave_drip_rnd.snd"), Drip)))
	{
		AddInfo(FString::Printf(TEXT("amb_cave_drip_rnd: samples=%d loop=%d..%d loopDelay mode %d %.2f..%.2f startDelay %.2f..%.2f category=%d 3D=%d dist=%.1f"),
			Drip.Samples.Num(), Drip.LoopCountMin, Drip.LoopCountMax, Drip.LoopDelay.Mode, Drip.LoopDelay.Min, Drip.LoopDelay.Max,
			Drip.StartDelay.Min, Drip.StartDelay.Max, Drip.CategoryValue, Drip.b3D, Drip.DistanceMeters));

		float SampleSeconds = 0.5f;
		const TArray<uint8> Bytes = Tre->ExtractFile(Drip.Samples[0]);
		FSWGDecodedPcm Pcm;
		if (FSWGWavDecoder::Decode(Bytes, Pcm))
		{
			SampleSeconds = Pcm.GetDurationSeconds();
		}

		const FSimulatedRun Run = Simulate(Drip, 3, SampleSeconds, 600.f, 12);
		TestTrue(TEXT("drip re-triggers repeatedly"), Run.StartTimes.Num() >= 8);
		bool bGapsInRange = true;
		float ShortestGap = FLT_MAX, LongestGap = 0.f;
		for (int32 Index = 1; Index < Run.StartTimes.Num(); ++Index)
		{
			const float Gap = Run.StartTimes[Index] - Run.EndTimes[Index - 1];
			ShortestGap = FMath::Min(ShortestGap, Gap);
			LongestGap = FMath::Max(LongestGap, Gap);
			bGapsInRange &= Gap >= 3.f - StepSeconds && Gap <= 6.f + 2.f * StepSeconds;
		}
		TestTrue(*FString::Printf(TEXT("drip gaps are 3-6 s (saw %.2f..%.2f)"), ShortestGap, LongestGap), bGapsInRange);
	}

	FSWGSoundTemplate Rollover;
	if (TestTrue(TEXT("ui_rollover.snd parses"), ReadTemplate(*Tre, TEXT("sound/ui_rollover.snd"), Rollover)))
	{
		const FSimulatedRun Run = Simulate(Rollover, 1, 0.3f, 60.f, 20);
		TestEqual(TEXT("ui_rollover plays once"), Run.StartTimes.Num(), 1);
		TestTrue(TEXT("ui_rollover finishes"), Run.bFinished);
		TestEqual(TEXT("ui_rollover is a UI sound"), Rollover.CategoryValue, static_cast<int32>(ESWGSoundCategory::UI));
	}

	// Mode 3 parameters and 3D distances actually present in the data, for tuning the drift guess.
	{
		TArray<FString> SoundPaths = Tre->FindVirtualPaths(TEXT(".snd"));
		SoundPaths.RemoveAll([](const FString& Path) { return !Path.EndsWith(TEXT(".snd")); });
		SoundPaths.Sort();
		int32 DriftSounds = 0;
		for (const FString& SoundPath : SoundPaths)
		{
			FSWGSoundTemplate Template;
			if (!ReadTemplate(*Tre, SoundPath, Template) || (Template.Volume.Mode != 3 && Template.Pitch.Mode != 3))
			{
				continue;
			}
			if (DriftSounds++ < 8)
			{
				AddInfo(FString::Printf(TEXT("mode3 %s: vol m%d [%.2f,%.2f] extra %.3f/%.3f | pitch m%d [%.2f,%.2f] extra %.3f/%.3f"), *SoundPath,
					Template.Volume.Mode, Template.Volume.Min, Template.Volume.Max, Template.Volume.ExtraA, Template.Volume.ExtraB,
					Template.Pitch.Mode, Template.Pitch.Min, Template.Pitch.Max, Template.Pitch.ExtraA, Template.Pitch.ExtraB));
			}
		}
		AddInfo(FString::Printf(TEXT("sounds using mode 3 drift: %d"), DriftSounds));
	}

	// ── music.iff loop units ──────────────────────────────────────────────
	{
		FSWGDataTableData Table;
		if (FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/music/music.iff")), Table))
		{
			AddInfo(FString::Printf(TEXT("music.iff: %d rows, columns: %s"), Table.Rows.Num(), *FString::Join(Table.ColumnNames, TEXT(" | "))));
			const int32 FileColumn = Table.GetColumnIndex(TEXT("File"));
			const int32 StartColumn = Table.GetColumnIndex(TEXT("Loop Start"));
			const int32 EndColumn = Table.GetColumnIndex(TEXT("Loop End"));
			if (TestTrue(TEXT("music.iff has File/Loop Start/Loop End columns"), FileColumn != INDEX_NONE && StartColumn != INDEX_NONE && EndColumn != INDEX_NONE))
			{
				int32 Checked = 0, InsideFrames = 0;
				for (const FSWGDataTableRow& Row : Table.Rows)
				{
					if (Checked >= 12)
					{
						break;
					}
					FString File = Row.Cells[FileColumn];
					if (File.IsEmpty())
					{
						continue;
					}
					if (!Tre->FileExists(File))
					{
						File = TEXT("music/") + File;
					}
					if (!Tre->FileExists(File) && !File.EndsWith(TEXT(".mp3")))
					{
						File += TEXT(".mp3");
					}
					FSWGDecodedPcm Pcm;
					if (!Tre->FileExists(File) || !FSWGMp3Decoder::Decode(Tre->ExtractFile(File), Pcm))
					{
						AddInfo(FString::Printf(TEXT("music.iff row '%s' not decodable"), *Row.Cells[FileColumn]));
						continue;
					}
					++Checked;
					const double LoopStart = FCString::Atod(*Row.Cells[StartColumn]);
					const double LoopEnd = FCString::Atod(*Row.Cells[EndColumn]);
					const double Frames = Pcm.GetNumFrames();
					InsideFrames += LoopEnd <= Frames ? 1 : 0;
					AddInfo(FString::Printf(TEXT("music %s: frames=%d rate=%d dur=%.2fs loop=[%s, %s] end/frames=%.4f end/rate=%.2fs"), *File, Pcm.GetNumFrames(),
						Pcm.SampleRate, Pcm.GetDurationSeconds(), *Row.Cells[StartColumn], *Row.Cells[EndColumn],
						Frames > 0 ? LoopEnd / Frames : 0.0, Pcm.SampleRate > 0 ? LoopEnd / Pcm.SampleRate : 0.0));
					(void)LoopStart;
				}
				AddInfo(FString::Printf(TEXT("music.iff: %d of %d checked rows have Loop End <= decoded frames"), InsideFrames, Checked));
			}
		}
		else
		{
			AddWarning(TEXT("datatables/music/music.iff did not parse"));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
