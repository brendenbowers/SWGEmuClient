#include "Playback/SWGSoundScheduler.h"

// Retail mode 3 (SWGEmu.exe 0x4199c0 pitch, 0x419b20 volume): every ExtraA seconds pick a new random
// target in [Min, Max], then glide linearly to it over ExtraB seconds (ExtraB <= 0 snaps).
void FSWGSoundScheduler::FDrift::Init(const FSWGSoundRandomRange& Range, FRandomStream& Stream)
{
	Current = Stream.FRandRange(Range.Min, FMath::Max(Range.Min, Range.Max));
	Start = Current;
	Target = Current;
	Elapsed = 0.f;
}

float FSWGSoundScheduler::FDrift::Advance(float DeltaSeconds, const FSWGSoundRandomRange& Range, FRandomStream& Stream)
{
	Elapsed += DeltaSeconds;
	if (Range.ExtraA <= Elapsed)
	{
		Elapsed = 0.f;
		Start = Current;
		Target = Stream.FRandRange(Range.Min, FMath::Max(Range.Min, Range.Max));
	}
	if (Current != Target)
	{
		if (Range.ExtraB <= 0.f)
		{
			Current = Target;
		}
		else
		{
			Current += (Target - Start) / Range.ExtraB * DeltaSeconds;
			Current = Target >= Start ? FMath::Min(Current, Target) : FMath::Max(Current, Target);
		}
	}
	return Current;
}

float FSWGSoundScheduler::DrawRange(const FSWGSoundRandomRange& Range, FRandomStream& Stream)
{
	if (Range.Mode == 3)
	{
		return 0.5f * (Range.Min + Range.Max);
	}
	// Start delay carries mode 0 yet still has a range, so any Min < Max is a uniform draw.
	return Range.Max > Range.Min ? Stream.FRandRange(Range.Min, Range.Max) : Range.Min;
}

float FSWGSoundScheduler::DrawFade(const FSWGSoundRandomRange& Range, FRandomStream& Stream)
{
	return Range.Mode == 0 ? 0.f : FMath::Max(0.f, DrawRange(Range, Stream));
}

float FSWGSoundScheduler::ComputeFadeGain(float ElapsedSeconds, float DurationSeconds, float FadeInSeconds, float FadeOutSeconds)
{
	float Gain = 1.f;
	if (FadeInSeconds > 0.f)
	{
		Gain *= FMath::Clamp(ElapsedSeconds / FadeInSeconds, 0.f, 1.f);
	}
	if (FadeOutSeconds > 0.f)
	{
		Gain *= FMath::Clamp((DurationSeconds - ElapsedSeconds) / FadeOutSeconds, 0.f, 1.f);
	}
	return Gain;
}

void FSWGSoundScheduler::Begin(const FSWGSoundTemplate& InTemplate, int32 Seed)
{
	Template = InTemplate;
	Stream.Initialize(Seed);
	StartedIterations = 0;
	LastSampleIndex = INDEX_NONE;

	bInfinite = Template.IsInfiniteLoop();
	const int32 LowCount = FMath::Max(1, FMath::Min(Template.LoopCountMin, Template.LoopCountMax));
	const int32 HighCount = FMath::Max(1, FMath::Max(Template.LoopCountMin, Template.LoopCountMax));
	TotalIterations = bInfinite ? 0 : Stream.RandRange(LowCount, HighCount);

	CurrentVolume = Template.Volume.Mode == 0 ? 1.f : DrawRange(Template.Volume, Stream);
	CurrentPitchSemitones = Template.Pitch.Mode == 0 ? 0.f : DrawRange(Template.Pitch, Stream);
	if (Template.Volume.Mode == 3)
	{
		VolumeDrift.Init(Template.Volume, Stream);
		CurrentVolume = VolumeDrift.Current;
	}
	if (Template.Pitch.Mode == 3)
	{
		PitchDrift.Init(Template.Pitch, Stream);
		CurrentPitchSemitones = PitchDrift.Current;
	}

	if (Template.Samples.IsEmpty())
	{
		Phase = EPhase::Finished;
		return;
	}

	PhaseTimer = FMath::Max(0.f, DrawRange(Template.StartDelay, Stream));
	Phase = EPhase::StartDelay;
}

void FSWGSoundScheduler::StartIteration(FSWGSoundIteration& OutIteration)
{
	const int32 SampleCount = Template.Samples.Num();
	int32 SampleIndex = Stream.RandHelper(SampleCount);
	// Avoid playing the same variation twice in a row.
	if (SampleCount > 1 && SampleIndex == LastSampleIndex)
	{
		SampleIndex = (SampleIndex + 1 + Stream.RandHelper(SampleCount - 1)) % SampleCount;
	}
	LastSampleIndex = SampleIndex;

	// Retail modes (0x41a7d0 volume, 0x41a940 pitch): 0 fixed, 1 drawn on the first iteration only, 2 every iteration.
	const bool bFirstIteration = StartedIterations == 0;
	if (Template.Volume.Mode == 2 || (Template.Volume.Mode == 1 && bFirstIteration))
	{
		CurrentVolume = FMath::Max(0.f, DrawRange(Template.Volume, Stream));
	}
	if (Template.Pitch.Mode == 2 || (Template.Pitch.Mode == 1 && bFirstIteration))
	{
		CurrentPitchSemitones = DrawRange(Template.Pitch, Stream);
	}

	OutIteration.SampleIndex = SampleIndex;
	OutIteration.Volume = CurrentVolume;
	OutIteration.PitchSemitones = CurrentPitchSemitones;
	OutIteration.PitchMultiplier = GetPitchMultiplier();
	// Fade modes (0x41a5f0 in, 0x41a6d0 out): 1 = first / last iteration only, 2 = every iteration.
	const bool bLastIteration = !bInfinite && StartedIterations == TotalIterations - 1;
	OutIteration.FadeInSeconds = Template.FadeIn.Mode == 2 || (Template.FadeIn.Mode == 1 && bFirstIteration) ? DrawFade(Template.FadeIn, Stream) : 0.f;
	OutIteration.FadeOutSeconds = Template.FadeOut.Mode == 2 || (Template.FadeOut.Mode == 1 && bLastIteration) ? DrawFade(Template.FadeOut, Stream) : 0.f;
	OutIteration.IterationNumber = StartedIterations++;
	Phase = EPhase::Playing;
}

bool FSWGSoundScheduler::Tick(float DeltaSeconds, FSWGSoundIteration& OutIteration)
{
	if (Phase == EPhase::NotStarted || Phase == EPhase::Finished)
	{
		return false;
	}

	if (Template.Volume.Mode == 3)
	{
		CurrentVolume = FMath::Max(0.f, VolumeDrift.Advance(DeltaSeconds, Template.Volume, Stream));
	}
	if (Template.Pitch.Mode == 3)
	{
		CurrentPitchSemitones = PitchDrift.Advance(DeltaSeconds, Template.Pitch, Stream);
	}

	if (Phase == EPhase::StartDelay || Phase == EPhase::LoopDelay)
	{
		PhaseTimer -= DeltaSeconds;
		if (PhaseTimer <= 0.f)
		{
			StartIteration(OutIteration);
			return true;
		}
	}
	return false;
}

void FSWGSoundScheduler::NotifyIterationFinished()
{
	if (Phase != EPhase::Playing)
	{
		return;
	}

	if (!bInfinite && StartedIterations >= TotalIterations)
	{
		Phase = EPhase::Finished;
		return;
	}

	PhaseTimer = Template.LoopDelay.Mode == 0 ? 0.f : FMath::Max(0.f, DrawRange(Template.LoopDelay, Stream));
	Phase = EPhase::LoopDelay;
}
