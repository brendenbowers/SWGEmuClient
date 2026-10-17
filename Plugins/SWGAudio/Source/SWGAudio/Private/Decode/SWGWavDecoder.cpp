#include "Decode/SWGWavDecoder.h"

namespace
{
	constexpr uint16 FormatPcm = 1;
	constexpr uint16 FormatExtensible = 0xFFFE;

	uint32 ReadU32(const uint8* Bytes)
	{
		return uint32(Bytes[0]) | (uint32(Bytes[1]) << 8) | (uint32(Bytes[2]) << 16) | (uint32(Bytes[3]) << 24);
	}

	uint16 ReadU16(const uint8* Bytes)
	{
		return uint16(Bytes[0]) | (uint16(Bytes[1]) << 8);
	}

	bool Fail(FString* OutError, const TCHAR* Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
		return false;
	}
}

bool FSWGWavDecoder::Decode(TConstArrayView<uint8> Data, FSWGDecodedPcm& OutPcm, FString* OutError)
{
	OutPcm = FSWGDecodedPcm();

	const int64 Size = Data.Num();
	if (Size < 12 || FMemory::Memcmp(Data.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(Data.GetData() + 8, "WAVE", 4) != 0)
	{
		return Fail(OutError, TEXT("not a RIFF/WAVE file"));
	}

	bool bHaveFormat = false;
	uint16 BitsPerSample = 0;
	const uint8* PcmBytes = nullptr;
	int64 PcmSize = 0;
	int64 SampleLoopStart = INDEX_NONE;
	int64 SampleLoopEnd = INDEX_NONE;

	int64 Position = 12;
	while (Position + 8 <= Size)
	{
		const uint8* ChunkHeader = Data.GetData() + Position;
		int64 ChunkSize = ReadU32(ChunkHeader + 4);
		const uint8* Body = ChunkHeader + 8;
		const int64 Available = Size - (Position + 8);
		if (ChunkSize > Available)
		{
			// A truncated final chunk (usually data) is decoded as far as it goes.
			ChunkSize = Available;
		}

		if (FMemory::Memcmp(ChunkHeader, "fmt ", 4) == 0 && ChunkSize >= 16)
		{
			uint16 FormatTag = ReadU16(Body);
			if (FormatTag == FormatExtensible && ChunkSize >= 26)
			{
				FormatTag = ReadU16(Body + 24); // first two bytes of the sub-format GUID
			}
			if (FormatTag != FormatPcm)
			{
				return Fail(OutError, TEXT("not PCM"));
			}
			OutPcm.NumChannels = ReadU16(Body + 2);
			OutPcm.SampleRate = static_cast<int32>(ReadU32(Body + 4));
			BitsPerSample = ReadU16(Body + 14);
			bHaveFormat = true;
		}
		else if (FMemory::Memcmp(ChunkHeader, "data", 4) == 0)
		{
			PcmBytes = Body;
			PcmSize = ChunkSize;
		}
		else if (FMemory::Memcmp(ChunkHeader, "smpl", 4) == 0 && ChunkSize >= 36)
		{
			// 36-byte header (loop count at +28), then 24-byte loops: id, type, start, end (inclusive), fraction, playCount.
			const uint32 LoopCount = ReadU32(Body + 28);
			if (LoopCount > 0 && ChunkSize >= 36 + 24)
			{
				SampleLoopStart = ReadU32(Body + 36 + 8);
				SampleLoopEnd = static_cast<int64>(ReadU32(Body + 36 + 12)) + 1;
			}
		}

		Position += 8 + ChunkSize + (ChunkSize & 1);
	}

	if (!bHaveFormat || !PcmBytes)
	{
		return Fail(OutError, TEXT("missing fmt or data chunk"));
	}
	if (OutPcm.NumChannels < 1 || OutPcm.NumChannels > 2 || OutPcm.SampleRate <= 0)
	{
		return Fail(OutError, TEXT("unsupported channel count or rate"));
	}

	if (BitsPerSample == 16)
	{
		const int64 SampleCount = PcmSize / 2;
		OutPcm.Samples.SetNumUninitialized(SampleCount);
		for (int64 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			OutPcm.Samples[SampleIndex] = static_cast<int16>(ReadU16(PcmBytes + SampleIndex * 2));
		}
	}
	else if (BitsPerSample == 8)
	{
		OutPcm.Samples.SetNumUninitialized(PcmSize);
		for (int64 SampleIndex = 0; SampleIndex < PcmSize; ++SampleIndex)
		{
			OutPcm.Samples[SampleIndex] = static_cast<int16>((int32(PcmBytes[SampleIndex]) - 128) << 8);
		}
	}
	else
	{
		return Fail(OutError, TEXT("unsupported bit depth"));
	}

	// Drop a trailing partial frame.
	OutPcm.Samples.SetNum(OutPcm.GetNumFrames() * OutPcm.NumChannels, EAllowShrinking::No);

	if (SampleLoopStart != INDEX_NONE && SampleLoopEnd > SampleLoopStart)
	{
		const int64 NumFrames = OutPcm.GetNumFrames();
		OutPcm.LoopStartFrame = static_cast<int32>(FMath::Min<int64>(SampleLoopStart, NumFrames));
		OutPcm.LoopEndFrame = static_cast<int32>(FMath::Min<int64>(SampleLoopEnd, NumFrames));
	}

	if (!OutPcm.IsValid())
	{
		return Fail(OutError, TEXT("no sample data"));
	}
	return true;
}
