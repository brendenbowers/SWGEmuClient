#include "TRE/SWGFxCommon.h"

#include "TRE/SWGIFFChunkReader.h"

namespace
{
	constexpr FSWGIffTag Tag0001 = SWG_IFF_TAG('0', '0', '0', '1');
	constexpr FSWGIffTag Tag0002 = SWG_IFF_TAG('0', '0', '0', '2');
	constexpr FSWGIffTag Tag0000 = SWG_IFF_TAG('0', '0', '0', '0');

	/** First non-form child of a version form (WVFM/CLRR/PTIM wrap a single data chunk). */
	bool FindVersionChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Form, FSWGIffChunk& OutChunk)
	{
		for (const FSWGIffChunk& Child : Reader.ReadChildren(Form))
		{
			if (!Child.IsForm())
			{
				OutChunk = Child;
				return true;
			}
		}
		return false;
	}
}

bool FSWGWaveform::IsConstant() const
{
	for (const FSWGWaveformPoint& Point : Points)
	{
		if (Point.Value != Points[0].Value || Point.RandomA != 0.f || Point.RandomB != 0.f)
		{
			return false;
		}
	}
	return true;
}

void FSWGWaveform::Evaluate(float Time, float& OutValue, float& OutRandomA, float& OutRandomB) const
{
	if (Points.IsEmpty())
	{
		OutValue = OutRandomA = OutRandomB = 0.f;
		return;
	}

	int32 NextIndex = 0;
	while (NextIndex < Points.Num() && Points[NextIndex].Time <= Time)
	{
		++NextIndex;
	}

	if (NextIndex == 0 || NextIndex == Points.Num())
	{
		const FSWGWaveformPoint& Edge = Points[NextIndex == 0 ? 0 : Points.Num() - 1];
		OutValue = Edge.Value;
		OutRandomA = Edge.RandomA;
		OutRandomB = Edge.RandomB;
		return;
	}

	const FSWGWaveformPoint& Previous = Points[NextIndex - 1];
	const FSWGWaveformPoint& Next = Points[NextIndex];
	const float Span = Next.Time - Previous.Time;
	const float Alpha = Span > UE_SMALL_NUMBER ? (Time - Previous.Time) / Span : 1.f;
	OutValue = FMath::Lerp(Previous.Value, Next.Value, Alpha);
	OutRandomA = FMath::Lerp(Previous.RandomA, Next.RandomA, Alpha);
	OutRandomB = FMath::Lerp(Previous.RandomB, Next.RandomB, Alpha);
}

float FSWGWaveform::EvaluateValue(float Time) const
{
	float Value, RandomA, RandomB;
	Evaluate(Time, Value, RandomA, RandomB);
	return Value;
}

float FSWGWaveform::Sample(float Time, float Random01) const
{
	float Value, RandomA, RandomB;
	Evaluate(Time, Value, RandomA, RandomB);
	if (SampleMode >= 2)
	{
		RandomA = RandomB = 0.f;
	}
	else if (SampleMode == 1)
	{
		// Retail rerolls on every sample; a hash of the persistent random and the time keeps that deterministic.
		Random01 = FMath::Frac(FMath::Sin(Random01 * 78.233f + Time * 12.9898f) * 43758.5453f);
	}
	const float Result = Value + FMath::Lerp(-RandomA, RandomB, Random01);
	return bHasClamp && ClampMin <= ClampMax ? FMath::Clamp(Result, ClampMin, ClampMax) : Result;
}

FLinearColor FSWGParticleColorRamp::Evaluate(float Time) const
{
	if (Points.IsEmpty())
	{
		return FLinearColor::White;
	}

	int32 NextIndex = 0;
	while (NextIndex < Points.Num() && Points[NextIndex].Time <= Time)
	{
		++NextIndex;
	}
	if (NextIndex == 0)
	{
		return Points[0].Color;
	}
	if (NextIndex == Points.Num())
	{
		return Points.Last().Color;
	}

	const FSWGParticleColorRampPoint& Previous = Points[NextIndex - 1];
	const FSWGParticleColorRampPoint& Next = Points[NextIndex];
	const float Span = Next.Time - Previous.Time;
	return FMath::Lerp(Previous.Color, Next.Color, Span > UE_SMALL_NUMBER ? (Time - Previous.Time) / Span : 1.f);
}

bool FSWGFxCommonReader::ReadWaveform(const FSWGIffReader& Reader, const FSWGIffChunk& WaveformForm, FSWGWaveform& OutWaveform)
{
	OutWaveform = FSWGWaveform();

	FSWGIffChunk DataChunk;
	if (!FindVersionChunk(Reader, WaveformForm, DataChunk) || (DataChunk.Tag != Tag0001 && DataChunk.Tag != Tag0002))
	{
		return false;
	}

	OutWaveform.Version = DataChunk.Tag == Tag0001 ? 1 : 2;
	FSWGIFFChunkReader Chunk(DataChunk, Reader);
	int32 Count = 0;
	bool bOk = Chunk.ReadValueLE(OutWaveform.InterpolationMode) && Chunk.ReadValueLE(OutWaveform.SampleMode);
	if (bOk && OutWaveform.Version == 2)
	{
		OutWaveform.bHasClamp = true;
		bOk = Chunk.ReadValueLE(OutWaveform.ClampMin) && Chunk.ReadValueLE(OutWaveform.ClampMax);
	}
	bOk = bOk && Chunk.ReadValueLE(Count) && Count >= 0 && Count <= Chunk.GetRemaining() / 16 && Count * 16 == Chunk.GetRemaining();
	if (!bOk)
	{
		return false;
	}

	OutWaveform.Points.SetNum(Count);
	for (FSWGWaveformPoint& Point : OutWaveform.Points)
	{
		Chunk.ReadValueLE(Point.Time);
		Chunk.ReadValueLE(Point.Value);
		Chunk.ReadValueLE(Point.RandomA);
		Chunk.ReadValueLE(Point.RandomB);
		if (OutWaveform.Version == 1)
		{
			// 0001 stores (up, down); 0002 and the struct use (down, up).
			Swap(Point.RandomA, Point.RandomB);
		}
	}
	return true;
}

bool FSWGFxCommonReader::ReadColorRamp(const FSWGIffReader& Reader, const FSWGIffChunk& RampForm, FSWGParticleColorRamp& OutRamp)
{
	OutRamp = FSWGParticleColorRamp();

	FSWGIffChunk DataChunk;
	if (!FindVersionChunk(Reader, RampForm, DataChunk) || DataChunk.Tag != Tag0001)
	{
		return false;
	}

	FSWGIFFChunkReader Chunk(DataChunk, Reader);
	int32 Count = 0;
	if (!Chunk.ReadValueLE(OutRamp.Mode0) || !Chunk.ReadValueLE(OutRamp.Mode1) || !Chunk.ReadValueLE(Count)
		|| Count < 0 || Count > Chunk.GetRemaining() / 16 || Count * 16 != Chunk.GetRemaining())
	{
		return false;
	}

	OutRamp.Points.SetNum(Count);
	for (FSWGParticleColorRampPoint& Point : OutRamp.Points)
	{
		Chunk.ReadValueLE(Point.Time);
		Chunk.ReadValueLE(Point.Color.R);
		Chunk.ReadValueLE(Point.Color.G);
		Chunk.ReadValueLE(Point.Color.B);
		Point.Color.A = 1.f;
	}
	return true;
}

bool FSWGFxCommonReader::ReadTiming(const FSWGIffReader& Reader, const FSWGIffChunk& TimingForm, FSWGEffectTiming& OutTiming)
{
	OutTiming = FSWGEffectTiming();

	FSWGIffChunk DataChunk;
	if (!FindVersionChunk(Reader, TimingForm, DataChunk) || DataChunk.Tag != Tag0001 || DataChunk.DataSize != 24)
	{
		return false;
	}

	FSWGIFFChunkReader Chunk(DataChunk, Reader);
	OutTiming.bPresent = true;
	return Chunk.ReadValueLE(OutTiming.StartDelayMin) && Chunk.ReadValueLE(OutTiming.StartDelayMax)
		&& Chunk.ReadValueLE(OutTiming.LoopDelayMin) && Chunk.ReadValueLE(OutTiming.LoopDelayMax)
		&& Chunk.ReadValueLE(OutTiming.LoopCountMin) && Chunk.ReadValueLE(OutTiming.LoopCountMax);
}

bool FSWGFxCommonReader::ReadTexture(const FSWGIffReader& Reader, const FSWGIffChunk& TextureForm, FSWGParticleTexture& OutTexture)
{
	OutTexture = FSWGParticleTexture();

	FSWGIffChunk DataChunk;
	if (!FindVersionChunk(Reader, TextureForm, DataChunk) || DataChunk.Tag != Tag0000)
	{
		return false;
	}

	FSWGIFFChunkReader Chunk(DataChunk, Reader);
	if (!Chunk.ReadTerminiatedString(OutTexture.ShaderPath) || Chunk.GetRemaining() != PtexTailSize)
	{
		return false;
	}
	NormalizePath(OutTexture.ShaderPath);

	const uint8* TailBytes = Reader.GetChunkData(DataChunk) + Chunk.GetPosition();
	OutTexture.RawTail.Append(TailBytes, PtexTailSize);

	uint8 Flag = 1;
	const bool bOk = Chunk.ReadValueLE(OutTexture.FrameCount) && Chunk.ReadValueLE(OutTexture.FrameStart)
		&& Chunk.ReadValueLE(OutTexture.FrameEnd) && Chunk.ReadValueLE(OutTexture.CellSize)
		&& Chunk.ReadValueLE(OutTexture.GridColumns) && Chunk.ReadValueLE(OutTexture.FramesPerSecond)
		&& Chunk.ReadValueLE(Flag);
	OutTexture.bFlag = Flag != 0;
	return bOk;
}
