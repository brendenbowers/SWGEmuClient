#pragma once

#include "CoreMinimal.h"
#include "Decode/SWGDecodedPcm.h"

/**
 * RIFF/WAVE PCM decoder for the retail samples (16-bit, mono or stereo, 11025 to 44100 Hz).
 * 8-bit PCM is widened to 16-bit. fmt/data/smpl are read; cue, LIST, inst and any other chunk is skipped.
 * The first smpl loop becomes the loop region.
 */
class SWGAUDIO_API FSWGWavDecoder
{
public:
	static bool Decode(TConstArrayView<uint8> Data, FSWGDecodedPcm& OutPcm, FString* OutError = nullptr);
};
