#pragma once

#include "CoreMinimal.h"
#include "Decode/SWGDecodedPcm.h"
#include <atomic>

/**
 * Reads one shared decoded sample into interleaved int16 blocks for a procedural wave.
 * Applies fade-in, fade-out (non-looping only) and a requested stop fade per frame.
 * Generate runs on the audio thread; everything else is thread-safe.
 */
class SWGAUDIO_API FSWGPcmStreamSource
{
public:
	struct FConfig
	{
		TSharedPtr<const FSWGDecodedPcm> Pcm;
		/** Loop forever: the sample's loop region if it has one (intro, then the region), else the whole sample. */
		bool bLoop = false;
		/** Frame overrides for the loop region (e.g. from music.iff); INDEX_NONE uses the file's own points. */
		int32 LoopStartFrame = INDEX_NONE;
		int32 LoopEndFrame = INDEX_NONE;
		float FadeInSeconds = 0.f;
		float FadeOutSeconds = 0.f;
	};

	explicit FSWGPcmStreamSource(const FConfig& InConfig);

	/** Fills NumFrames frames; silence once exhausted. */
	void Generate(int16* OutSamples, int32 NumFrames);

	/** Ramps to silence over FadeSeconds, then reports exhausted. */
	void RequestStop(float FadeSeconds);

	/** True once every frame has been handed to the mixer (or the stop fade finished). */
	bool IsExhausted() const { return bExhausted.load(std::memory_order_acquire); }

	int32 GetNumChannels() const { return Config.Pcm ? Config.Pcm->NumChannels : 0; }
	int32 GetSampleRate() const { return Config.Pcm ? Config.Pcm->SampleRate : 0; }
	bool IsLooping() const { return Config.bLoop; }

	/** Resolved loop region in frames; start == end when not looping. */
	int32 GetLoopStartFrame() const { return LoopStartFrame; }
	int32 GetLoopEndFrame() const { return LoopEndFrame; }

private:
	FConfig Config;
	int32 NumFrames = 0;
	int32 LoopStartFrame = 0;
	int32 LoopEndFrame = 0;
	int32 FadeInFrames = 0;
	int32 FadeOutFrames = 0;

	// Audio-thread state.
	int32 Cursor = 0;
	int64 PlayedFrames = 0;
	int32 StopFramesTotal = 0;
	int32 StopFramesDone = 0;
	bool bStopping = false;

	std::atomic<int32> StopRequestFrames{ -1 };
	std::atomic<bool> bExhausted{ false };
};
