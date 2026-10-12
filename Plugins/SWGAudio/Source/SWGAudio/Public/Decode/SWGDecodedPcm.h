#pragma once

#include "CoreMinimal.h"

/** Fully decoded sample: 16-bit interleaved PCM plus optional loop points. */
struct SWGAUDIO_API FSWGDecodedPcm
{
	TArray<int16> Samples;
	int32 NumChannels = 0;
	int32 SampleRate = 0;

	/** Loop region in frames, [LoopStartFrame, LoopEndFrame); INDEX_NONE when the file has none (WAV smpl chunk). */
	int32 LoopStartFrame = INDEX_NONE;
	int32 LoopEndFrame = INDEX_NONE;

	int32 GetNumFrames() const { return NumChannels > 0 ? Samples.Num() / NumChannels : 0; }
	float GetDurationSeconds() const { return SampleRate > 0 ? static_cast<float>(GetNumFrames()) / SampleRate : 0.f; }
	bool HasLoop() const { return LoopStartFrame != INDEX_NONE && LoopEndFrame > LoopStartFrame; }
	bool IsValid() const { return NumChannels > 0 && SampleRate > 0 && Samples.Num() > 0; }
};
