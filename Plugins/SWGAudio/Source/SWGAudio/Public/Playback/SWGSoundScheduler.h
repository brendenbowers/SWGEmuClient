#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "TRE/SWGSoundTemplateReader.h"

/** One iteration the scheduler wants started: which sample and with which randomised parameters. */
struct FSWGSoundIteration
{
	int32 SampleIndex = 0;
	float Volume = 1.f;
	float PitchSemitones = 0.f;
	float PitchMultiplier = 1.f;
	float FadeInSeconds = 0.f;
	float FadeOutSeconds = 0.f;
	/** 0-based count of iterations started so far. */
	int32 IterationNumber = 0;
};

/**
 * Pure .snd playback state machine (no UE audio objects): start delay, then N iterations
 * (loop count, -1 = infinite) separated by a loop delay. The driver reports the end of each
 * iteration with NotifyIterationFinished() since only it knows the sample length.
 */
class SWGAUDIO_API FSWGSoundScheduler
{
public:
	enum class EPhase : uint8
	{
		NotStarted,
		StartDelay,
		Playing,
		LoopDelay,
		Finished,
	};

	/** A fixed seed gives a deterministic sequence. */
	void Begin(const FSWGSoundTemplate& InTemplate, int32 Seed);

	/** True when OutIteration should be started now. Also advances volume/pitch drift. */
	bool Tick(float DeltaSeconds, FSWGSoundIteration& OutIteration);

	void NotifyIterationFinished();

	/** Ends the sound regardless of remaining loops. */
	void Stop() { Phase = EPhase::Finished; }

	EPhase GetPhase() const { return Phase; }
	bool IsFinished() const { return Phase == EPhase::Finished; }
	bool IsInfinite() const { return bInfinite; }
	/** Iterations to run in total, or INDEX_NONE when infinite. */
	int32 GetTotalIterations() const { return bInfinite ? INDEX_NONE : TotalIterations; }
	int32 GetStartedIterations() const { return StartedIterations; }

	/** Current volume (0..1 scale) including slow drift; the driver applies it each frame. */
	float GetVolume() const { return CurrentVolume; }
	float GetPitchMultiplier() const { return FMath::Pow(2.f, CurrentPitchSemitones / 12.f); }

	/** Linear fade envelope at ElapsedSeconds into a sound of DurationSeconds. */
	static float ComputeFadeGain(float ElapsedSeconds, float DurationSeconds, float FadeInSeconds, float FadeOutSeconds);

	/** One draw from Range. Mode 3 returns the midpoint; the live drift is handled by Tick. */
	static float DrawRange(const FSWGSoundRandomRange& Range, FRandomStream& Stream);

	/** Draw for a fade duration: mode 0 means no fade. */
	static float DrawFade(const FSWGSoundRandomRange& Range, FRandomStream& Stream);

private:
	/** Retail mode 3: a new random target every ExtraA seconds, reached linearly over ExtraB seconds. */
	struct FDrift
	{
		float Current = 0.f;
		float Start = 0.f;
		float Target = 0.f;
		float Elapsed = 0.f;

		void Init(const FSWGSoundRandomRange& Range, FRandomStream& Stream);
		float Advance(float DeltaSeconds, const FSWGSoundRandomRange& Range, FRandomStream& Stream);
	};

	void StartIteration(FSWGSoundIteration& OutIteration);

	FSWGSoundTemplate Template;
	FRandomStream Stream;
	EPhase Phase = EPhase::NotStarted;
	bool bInfinite = false;
	int32 TotalIterations = 1;
	int32 StartedIterations = 0;
	int32 LastSampleIndex = INDEX_NONE;
	float PhaseTimer = 0.f;

	float CurrentVolume = 1.f;
	float CurrentPitchSemitones = 0.f;
	FDrift VolumeDrift;
	FDrift PitchDrift;
};
