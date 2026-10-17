#include "Decode/SWGMp3Decoder.h"

THIRD_PARTY_INCLUDES_START
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
THIRD_PARTY_INCLUDES_END

bool FSWGMp3Decoder::Decode(TConstArrayView<uint8> Data, FSWGDecodedPcm& OutPcm, FString* OutError)
{
	OutPcm = FSWGDecodedPcm();

	mp3dec_t Decoder;
	mp3dec_init(&Decoder);

	mp3d_sample_t FramePcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	const uint8* Cursor = Data.GetData();
	int32 Remaining = Data.Num();

	while (Remaining > 0)
	{
		mp3dec_frame_info_t FrameInfo;
		const int32 FramesDecoded = mp3dec_decode_frame(&Decoder, Cursor, Remaining, FramePcm, &FrameInfo);
		if (FrameInfo.frame_bytes <= 0)
		{
			break; // no further sync
		}
		Cursor += FrameInfo.frame_bytes;
		Remaining -= FrameInfo.frame_bytes;

		if (FramesDecoded <= 0)
		{
			continue; // skipped junk or an ID3 block
		}

		if (OutPcm.NumChannels == 0)
		{
			OutPcm.NumChannels = FrameInfo.channels;
			OutPcm.SampleRate = FrameInfo.hz;
		}
		else if (FrameInfo.channels != OutPcm.NumChannels || FrameInfo.hz != OutPcm.SampleRate)
		{
			continue; // stray frame in a different format
		}

		OutPcm.Samples.Append(FramePcm, FramesDecoded * FrameInfo.channels);
	}

	if (!OutPcm.IsValid())
	{
		if (OutError)
		{
			*OutError = TEXT("no decodable mp3 frames");
		}
		return false;
	}
	return true;
}
