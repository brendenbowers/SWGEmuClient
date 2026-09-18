#include "TRE/SWGColorRampReader.h"

FLinearColor FSWGColorRamp::Sample(ERow Row, float DayFraction) const
{
	if (!IsValid() || Row < 0 || Row >= RowCount)
	{
		return FLinearColor::Black;
	}

	const float Position = FMath::Frac(DayFraction) * Width;
	const int32 Column = FMath::FloorToInt(Position);
	const float Blend = Position - Column;
	const FColor& Left = Pixels[Row * Width + (Column % Width)];
	const FColor& Right = Pixels[Row * Width + ((Column + 1) % Width)];
	// sRGB bytes, like every other retail colour.
	return FMath::Lerp(FLinearColor(Left), FLinearColor(Right), Blend);
}

bool FSWGColorRampReader::ReadTga(const TArray<uint8>& Bytes, FSWGColorRamp& OutRamp)
{
	OutRamp = FSWGColorRamp();
	if (Bytes.Num() < 18)
	{
		return false;
	}

	const uint8 IdLength = Bytes[0];
	const uint8 ImageType = Bytes[2];
	const int32 Width = Bytes[12] | (Bytes[13] << 8);
	const int32 Height = Bytes[14] | (Bytes[15] << 8);
	const int32 BitsPerPixel = Bytes[16];
	const bool bTopDown = (Bytes[17] & 0x20) != 0;
	if (ImageType != 2 || (BitsPerPixel != 32 && BitsPerPixel != 24) || Width <= 0 || Height != FSWGColorRamp::RowCount)
	{
		return false;
	}

	const int32 BytesPerPixel = BitsPerPixel / 8;
	const int32 PixelStart = 18 + IdLength;
	if (Bytes.Num() < PixelStart + Width * Height * BytesPerPixel)
	{
		return false;
	}

	OutRamp.Width = Width;
	OutRamp.Pixels.SetNumUninitialized(Width * Height);
	for (int32 Row = 0; Row < Height; ++Row)
	{
		const int32 SourceRow = bTopDown ? Row : Height - 1 - Row;
		const uint8* Line = Bytes.GetData() + PixelStart + SourceRow * Width * BytesPerPixel;
		for (int32 Column = 0; Column < Width; ++Column)
		{
			const uint8* Pixel = Line + Column * BytesPerPixel;
			OutRamp.Pixels[Row * Width + Column] = FColor(Pixel[2], Pixel[1], Pixel[0], BytesPerPixel == 4 ? Pixel[3] : 255);
		}
	}
	return true;
}
