#include "Playback/SWGPcmStreamSource.h"

FSWGPcmStreamSource::FSWGPcmStreamSource(const FConfig& InConfig)
	: Config(InConfig)
{
	if (!Config.Pcm || !Config.Pcm->IsValid())
	{
		bExhausted = true;
		return;
	}

	NumFrames = Config.Pcm->GetNumFrames();
	const int32 SampleRate = Config.Pcm->SampleRate;
	FadeInFrames = FMath::RoundToInt(Config.FadeInSeconds * SampleRate);
	// A looping sound never reaches its end, so fade-out only applies to one-shots.
	FadeOutFrames = Config.bLoop ? 0 : FMath::RoundToInt(Config.FadeOutSeconds * SampleRate);

	if (Config.bLoop)
	{
		int32 Start = Config.LoopStartFrame;
		int32 End = Config.LoopEndFrame;
		if (Start == INDEX_NONE || End == INDEX_NONE)
		{
			Start = Config.Pcm->LoopStartFrame;
			End = Config.Pcm->LoopEndFrame;
		}
		End = FMath::Min(End, NumFrames);
		if (Start == INDEX_NONE || End == INDEX_NONE || Start < 0 || End <= Start)
		{
			Start = 0;
			End = NumFrames;
		}
		LoopStartFrame = Start;
		LoopEndFrame = End;
	}
}

void FSWGPcmStreamSource::RequestStop(float FadeSeconds)
{
	const int32 SampleRate = Config.Pcm ? Config.Pcm->SampleRate : 0;
	StopRequestFrames.store(FMath::Max(1, FMath::RoundToInt(FadeSeconds * SampleRate)), std::memory_order_release);
}

void FSWGPcmStreamSource::Generate(int16* OutSamples, int32 OutFrames)
{
	const int32 Channels = GetNumChannels();
	if (Channels <= 0 || bExhausted.load(std::memory_order_acquire))
	{
		FMemory::Memzero(OutSamples, sizeof(int16) * OutFrames * FMath::Max(Channels, 1));
		return;
	}

	if (!bStopping)
	{
		const int32 StopFrames = StopRequestFrames.load(std::memory_order_acquire);
		if (StopFrames > 0)
		{
			bStopping = true;
			StopFramesTotal = StopFrames;
		}
	}

	const int16* Source = Config.Pcm->Samples.GetData();
	int32 FrameIndex = 0;
	for (; FrameIndex < OutFrames; ++FrameIndex)
	{
		if (Config.bLoop)
		{
			if (Cursor >= LoopEndFrame)
			{
				Cursor = LoopStartFrame;
			}
		}
		else if (Cursor >= NumFrames)
		{
			bExhausted.store(true, std::memory_order_release);
			break;
		}

		float Gain = 1.f;
		if (PlayedFrames < FadeInFrames)
		{
			Gain *= static_cast<float>(PlayedFrames) / FadeInFrames;
		}
		if (FadeOutFrames > 0 && NumFrames - Cursor < FadeOutFrames)
		{
			Gain *= static_cast<float>(NumFrames - Cursor) / FadeOutFrames;
		}
		if (bStopping)
		{
			Gain *= 1.f - static_cast<float>(StopFramesDone) / StopFramesTotal;
			++StopFramesDone;
		}

		const int16* Frame = Source + static_cast<int64>(Cursor) * Channels;
		for (int32 Channel = 0; Channel < Channels; ++Channel)
		{
			OutSamples[FrameIndex * Channels + Channel] = static_cast<int16>(Frame[Channel] * Gain);
		}
		++Cursor;
		++PlayedFrames;

		if (bStopping && StopFramesDone >= StopFramesTotal)
		{
			bExhausted.store(true, std::memory_order_release);
			++FrameIndex;
			break;
		}
	}

	if (FrameIndex < OutFrames)
	{
		FMemory::Memzero(OutSamples + FrameIndex * Channels, sizeof(int16) * (OutFrames - FrameIndex) * Channels);
	}
}
