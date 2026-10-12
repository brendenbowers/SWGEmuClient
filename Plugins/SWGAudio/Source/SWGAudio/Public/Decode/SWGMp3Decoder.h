#pragma once

#include "CoreMinimal.h"
#include "Decode/SWGDecodedPcm.h"

/** Whole-file MP3 decoder (vendored minimp3). Leading ID3 tags and junk between frames are skipped. */
class SWGAUDIO_API FSWGMp3Decoder
{
public:
	static bool Decode(TConstArrayView<uint8> Data, FSWGDecodedPcm& OutPcm, FString* OutError = nullptr);
};
