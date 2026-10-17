#include "TRE/SWGSoundTemplateReader.h"

#include "TRE/SWGIFFChunkReader.h"

namespace
{
	float DwordToFloat(uint32 Dword)
	{
		float Value;
		FMemory::Memcpy(&Value, &Dword, sizeof(float));
		return Value;
	}

	void SetRange(FSWGSoundRandomRange& Range, int32 Mode, float Min, float Max, float ExtraA = 0.f, float ExtraB = 0.f)
	{
		Range.Mode = Mode;
		Range.Min = Min;
		Range.Max = Max;
		Range.ExtraA = ExtraA;
		Range.ExtraB = ExtraB;
	}
}

bool FSWGSoundTemplateReader::Read(const FSWGIffReader& Reader, FSWGSoundTemplate& OutTemplate)
{
	OutTemplate = FSWGSoundTemplate();

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (!Reader.IsValid() || TopLevel.IsEmpty() || !TopLevel[0].IsForm())
	{
		return false;
	}

	const FSWGIffChunk& Root = TopLevel[0];
	FSWGIffChunk Form2D;
	if (Root.FormType == SWG_IFF_TAG('S', 'D', '3', 'D'))
	{
		OutTemplate.b3D = true;
		FSWGIffChunk Chunk3D;
		if (Reader.FindChildChunk(Root, SWG_IFF_TAG('0', '0', '0', '1'), Chunk3D))
		{
			OutTemplate.Chunk3DSize = Chunk3D.DataSize;
		}
		if (!Reader.FindChildForm(Root, SWG_IFF_TAG('S', 'D', '2', 'D'), Form2D))
		{
			return false;
		}
	}
	else if (Root.FormType == SWG_IFF_TAG('S', 'D', '2', 'D'))
	{
		Form2D = Root;
	}
	else
	{
		return false;
	}

	FSWGIffChunk DataChunk;
	if (!Reader.FindChildChunk(Form2D, SWG_IFF_TAG('0', '0', '0', '3'), DataChunk))
	{
		return false;
	}

	FSWGIFFChunkReader Chunk(DataChunk, Reader);
	int32 SampleCount = 0;
	if (!Chunk.ReadValueLE(SampleCount) || SampleCount < 0 || SampleCount > DataChunk.DataSize)
	{
		return false;
	}

	OutTemplate.Samples.Reserve(SampleCount);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		FString Path;
		if (!Chunk.ReadTerminiatedString(Path))
		{
			return false;
		}
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		OutTemplate.Samples.Add(MoveTemp(Path));
	}

	TArray<uint32>& Tail = OutTemplate.RawTail;
	Tail.SetNumUninitialized(TailDwordCount);
	for (int32 DwordIndex = 0; DwordIndex < TailDwordCount; ++DwordIndex)
	{
		if (!Chunk.ReadValueLE(Tail[DwordIndex]))
		{
			return false;
		}
	}

	auto AsFloat = [&Tail](int32 Index) { return DwordToFloat(Tail[Index]); };
	auto AsInt = [&Tail](int32 Index) { return static_cast<int32>(Tail[Index]); };

	SetRange(OutTemplate.StartDelay, 0, AsFloat(0), AsFloat(1));
	SetRange(OutTemplate.FadeIn, AsInt(14), AsFloat(2), AsFloat(3));
	OutTemplate.LoopCountMin = AsInt(4);
	OutTemplate.LoopCountMax = AsInt(5);
	SetRange(OutTemplate.LoopDelay, AsInt(13), AsFloat(6), AsFloat(7));
	SetRange(OutTemplate.FadeOut, AsInt(15), AsFloat(8), AsFloat(9));
	OutTemplate.CategoryValue = AsInt(10);
	OutTemplate.UnknownFlag11 = AsInt(11);
	OutTemplate.Unknown12 = AsInt(12);
	SetRange(OutTemplate.Volume, AsInt(16), AsFloat(18), AsFloat(19), AsFloat(17), AsFloat(20));
	SetRange(OutTemplate.Pitch, AsInt(21), AsFloat(23), AsFloat(24), AsFloat(22), AsFloat(25));
	OutTemplate.Unknown26 = AsInt(26);
	OutTemplate.DistanceMeters = AsFloat(27);

	return true;
}
