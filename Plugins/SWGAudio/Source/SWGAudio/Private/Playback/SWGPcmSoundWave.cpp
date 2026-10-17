#include "Playback/SWGPcmSoundWave.h"

#include "Playback/SWGPcmStreamSource.h"

namespace
{
	// Same sentinel the engine uses (AudioDefines.h) so the wave is never cut short by its duration.
	constexpr float IndefiniteDurationSeconds = 10000.f;
}

USWGPcmSoundWave::USWGPcmSoundWave(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bLooping = false;
	Duration = IndefiniteDurationSeconds;
}

void USWGPcmSoundWave::Setup(TSharedPtr<FSWGPcmStreamSource> InSource)
{
	Source = MoveTemp(InSource);
	if (Source)
	{
		NumChannels = Source->GetNumChannels();
		SetSampleRate(Source->GetSampleRate());
	}
}

int32 USWGPcmSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	if (!Source || NumChannels <= 0)
	{
		return 0;
	}

	const int32 Frames = NumSamples / NumChannels;
	const int32 SamplesToWrite = Frames * NumChannels;
	OutAudio.SetNumUninitialized(SamplesToWrite * sizeof(int16), EAllowShrinking::No);
	Source->Generate(reinterpret_cast<int16*>(OutAudio.GetData()), Frames);
	return SamplesToWrite;
}
