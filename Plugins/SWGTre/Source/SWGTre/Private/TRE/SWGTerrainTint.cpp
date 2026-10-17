#include "TRE/SWGTerrainTint.h"

namespace
{
	uint8 ScaleChannel(uint8 Channel, float Amount)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::TruncToInt(Channel * Amount), 0, 255));
	}

	void CollectFromLayer(const FSWGTerrainLayer& Layer, TArray<FString>& OutPaths)
	{
		for (const FSWGTerrainAffector& Affector : Layer.Affectors)
		{
			const bool bRamp = Affector.Type == ESWGTerrainAffectorType::ColorRampHeight || Affector.Type == ESWGTerrainAffectorType::ColorRampFractal;
			if (bRamp && !Affector.ColorRampPath.IsEmpty())
			{
				OutPaths.AddUnique(Affector.ColorRampPath);
			}
		}
		for (const FSWGTerrainLayer& Child : Layer.Children)
		{
			CollectFromLayer(Child, OutPaths);
		}
	}
}

FColor SWGTerrainTint::Blend(const FColor& Existing, const FColor& NewColour, int32 Operation, float Amount)
{
	FColor Scaled = NewColour;
	if (Amount < 1.0f)
	{
		Scaled = FColor(ScaleChannel(NewColour.R, Amount), ScaleChannel(NewColour.G, Amount), ScaleChannel(NewColour.B, Amount));
	}

	auto Lerp = [Amount](uint8 Old, uint8 New, bool bHalfway)
	{
		const float Target = bHalfway ? (Old + New) * 0.5f : static_cast<float>(New);
		return static_cast<uint8>(FMath::Clamp(FMath::TruncToInt(Target * Amount + Old * (1.0f - Amount)), 0, 255));
	};

	switch (Operation)
	{
		case 1:
			return FColor(FMath::Min(Existing.R + Scaled.R, 255), FMath::Min(Existing.G + Scaled.G, 255), FMath::Min(Existing.B + Scaled.B, 255));
		case 2:
			return FColor(FMath::Max(Existing.R - Scaled.R, 0), FMath::Max(Existing.G - Scaled.G, 0), FMath::Max(Existing.B - Scaled.B, 0));
		case 3:
			return FColor(Lerp(Existing.R, NewColour.R, true), Lerp(Existing.G, NewColour.G, true), Lerp(Existing.B, NewColour.B, true));
		case 4:
			return Existing;
		default:
			return FColor(Lerp(Existing.R, NewColour.R, false), Lerp(Existing.G, NewColour.G, false), Lerp(Existing.B, NewColour.B, false));
	}
}

bool SWGTerrainTint::SampleRamp(const FSWGTerrainColorRamp& Ramp, float Fraction, FColor& OutColor)
{
	const int32 PixelIndex = FMath::TruncToInt(Fraction * (Ramp.Pixels.Num() - 1));
	if (!Ramp.Pixels.IsValidIndex(PixelIndex))
	{
		return false;
	}
	OutColor = Ramp.Pixels[PixelIndex];
	return true;
}

bool SWGTerrainTint::DecodeTga(TConstArrayView<uint8> Bytes, FSWGTerrainColorRamp& OutRamp)
{
	constexpr int32 HeaderSize = 18;
	if (Bytes.Num() < HeaderSize)
	{
		return false;
	}

	const int32 IdLength = Bytes[0];
	const int32 ColorMapType = Bytes[1];
	const int32 ImageType = Bytes[2];
	const int32 Width = Bytes[12] | (Bytes[13] << 8);
	const int32 Height = Bytes[14] | (Bytes[15] << 8);
	const int32 BitsPerPixel = Bytes[16];
	const bool bTopDown = (Bytes[17] & 0x20) != 0;
	if ((ImageType != 2 && ImageType != 10) || ColorMapType != 0 || (BitsPerPixel != 24 && BitsPerPixel != 32) || Width <= 0 || Height <= 0)
	{
		return false;
	}

	const int32 BytesPerPixel = BitsPerPixel / 8;
	int32 Offset = HeaderSize + IdLength;
	const int32 PixelCount = Width * Height;
	TArray<FColor> Pixels;
	Pixels.Reserve(PixelCount);

	auto ReadPixel = [&](int32 At) { return FColor(Bytes[At + 2], Bytes[At + 1], Bytes[At], 255); };

	if (ImageType == 2)
	{
		if (Offset + PixelCount * BytesPerPixel > Bytes.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < PixelCount; ++Index)
		{
			Pixels.Add(ReadPixel(Offset + Index * BytesPerPixel));
		}
	}
	else
	{
		while (Pixels.Num() < PixelCount && Offset < Bytes.Num())
		{
			const int32 Packet = Bytes[Offset++];
			const int32 Count = (Packet & 0x7f) + 1;
			const bool bRun = (Packet & 0x80) != 0;
			if (Offset + (bRun ? BytesPerPixel : Count * BytesPerPixel) > Bytes.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < Count && Pixels.Num() < PixelCount; ++Index)
			{
				Pixels.Add(ReadPixel(Offset + (bRun ? 0 : Index * BytesPerPixel)));
			}
			Offset += bRun ? BytesPerPixel : Count * BytesPerPixel;
		}
		if (Pixels.Num() != PixelCount)
		{
			return false;
		}
	}

	// Row 0 is the visual top row, as FSWGColorRampReader treats the environment ramps.
	const int32 TopRow = bTopDown ? 0 : Height - 1;
	OutRamp.Pixels.Reset();
	OutRamp.Pixels.Append(Pixels.GetData() + TopRow * Width, Width);
	return true;
}

void SWGTerrainTint::CollectRampPaths(const FSWGTerrainData& Data, TArray<FString>& OutPaths)
{
	for (const FSWGTerrainLayer& Layer : Data.TopLevelLayers)
	{
		CollectFromLayer(Layer, OutPaths);
	}
}
