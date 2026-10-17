#include "Common/SWGPlanetGlobeBake.h"
#include "Common/SWGTerrainBake.h"
#include "Async/ParallelFor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

namespace
{
	constexpr uint32 CacheMagic = 0x42474C47; // "GLGB"
	constexpr uint32 CacheVersion = 2;

	struct FCacheHeader
	{
		uint32 Magic = CacheMagic;
		uint32 Version = CacheVersion;
		int32 CapResolution = 0;
		float CapRadius = 0;
		float Fill = 0;
		uint32 Seed = 0;
		double BuildSeconds = 0;
	};

	float HashToUnit(int32 CellX, int32 CellY, uint32 Seed)
	{
		uint32 Hash = uint32(CellX)*374761393u + uint32(CellY)*668265263u + Seed*2246822519u;
		Hash = (Hash ^ (Hash >> 13))*1274126177u;
		Hash ^= Hash >> 16;
		return float(Hash & 0xFFFFFF)/float(0x1000000);
	}

	/** Smooth value noise in -1..1. */
	float Noise(float X, float Y, uint32 Seed)
	{
		const float FloorX = FMath::FloorToFloat(X), FloorY = FMath::FloorToFloat(Y);
		const int32 CellX = int32(FloorX), CellY = int32(FloorY);
		const float FractionX = FMath::SmoothStep(0.f, 1.f, X - FloorX), FractionY = FMath::SmoothStep(0.f, 1.f, Y - FloorY);
		const float Low = FMath::Lerp(HashToUnit(CellX, CellY, Seed), HashToUnit(CellX + 1, CellY, Seed), FractionX);
		const float High = FMath::Lerp(HashToUnit(CellX, CellY + 1, Seed), HashToUnit(CellX + 1, CellY + 1, Seed), FractionX);
		return FMath::Lerp(Low, High, FractionY)*2.f - 1.f;
	}

	/** Folds a coordinate back into -Half..Half by reflecting at each end, so the field stays continuous across the edge. */
	float Mirror(float Value, float Half)
	{
		const float Scaled = (Value + Half)/(2.f*Half);
		float Folded = Scaled - 2.f*FMath::FloorToFloat(Scaled*.5f);
		if (Folded > 1.f) Folded = 2.f - Folded;
		return Folded*2.f*Half - Half;
	}

	/** The real bake (colour kept as sRGB bytes: a 4096 map as floats would cost 270 MB), heights in metres, and bilinear lookups by ground position. */
	struct FRealField
	{
		int32 Width = 0, Height = 0;
		float MapSize = 0, Usable = 0;
		bool bFallbackMap = false;
		TArray<FColor> Colour;
		TArray<float> Elevation;
		const FSWGImage* DetailWeights = nullptr;
		uint32 Seed = 0;

		void Load(const FSWGTerrainBakeResult& Real)
		{
			Width = Real.Colour.Width; Height = Real.Colour.Height; MapSize = Real.MapSize > 0 ? Real.MapSize : 16384.f;
			Usable = MapSize*.5f*FSWGPlanetGlobeBake::UsableFraction;
			bFallbackMap = Real.bFallbackMap;
			const int32 PixelCount = Width*Height;
			Colour = Real.Colour.Pixels;
			Elevation.SetNumZeroed(PixelCount);
			if (Real.Heights.Num() == PixelCount)
			{
				const float Range = Real.MaxHeight - Real.MinHeight;
				for (int32 Index = 0; Index < PixelCount; ++Index) Elevation[Index] = Real.MinHeight + Real.Heights[Index]/65535.f*Range;
			}
			if (Real.DetailWeights.IsValid()) DetailWeights = &Real.DetailWeights;
		}

		/** Where a ground position reads the real map: itself inside the usable square, a warped mirror of it outside (Beyond 0..1 says how far out). */
		void MapPosition(float GroundX, float GroundY, float& OutSampleX, float& OutSampleY, float& OutBeyond) const
		{
			const float Distance = FMath::Max(FMath::Abs(GroundX), FMath::Abs(GroundY));
			OutBeyond = FMath::SmoothStep(0.f, 1.f, (Distance - Usable)/Usable);
			float SampleX = GroundX, SampleY = GroundY;
			if (OutBeyond > 0.f)
			{
				// Two octaves of domain warp that fade in over the first usable width past the edge: the mirror is seamless at the edge and
				// stops reading as a mirror further out.
				const float Coarse = 1.f/(Usable*.9f), Fine = 1.f/(Usable*.3f);
				SampleX += OutBeyond*(Usable*.45f*Noise(GroundX*Coarse, GroundY*Coarse, Seed + 1) + Usable*.12f*Noise(GroundX*Fine, GroundY*Fine, Seed + 2));
				SampleY += OutBeyond*(Usable*.45f*Noise(GroundX*Coarse + 31.7f, GroundY*Coarse - 12.3f, Seed + 3) + Usable*.12f*Noise(GroundX*Fine - 7.1f, GroundY*Fine + 19.9f, Seed + 4));
			}
			OutSampleX = Mirror(SampleX, Usable); OutSampleY = Mirror(SampleY, Usable);
		}

		void SampleMap(float GroundX, float GroundY, FLinearColor& OutColour, float& OutElevation) const
		{
			const float Column = FMath::Clamp((GroundX/MapSize + .5f)*Width - .5f, 0.f, Width - 1.f);
			const float Row = FMath::Clamp((.5f - GroundY/MapSize)*Height - .5f, 0.f, Height - 1.f);
			const int32 Column0 = int32(Column), Row0 = int32(Row);
			const int32 Column1 = FMath::Min(Column0 + 1, Width - 1), Row1 = FMath::Min(Row0 + 1, Height - 1);
			const float FractionX = Column - Column0, FractionY = Row - Row0;
			auto Blend = [&](const auto& Values)
			{
				return FMath::Lerp(FMath::Lerp(Values[Row0*Width + Column0], Values[Row0*Width + Column1], FractionX),
					FMath::Lerp(Values[Row1*Width + Column0], Values[Row1*Width + Column1], FractionX), FractionY);
			};
			auto Linear = [&](int32 Index)
			{
				const FColor Pixel = Colour[Index];
				FLinearColor Result = FLinearColor::FromSRGBColor(Pixel);
				// A fallback overhead map has no water classification.
				Result.A = bFallbackMap ? 0.f : Pixel.A/255.f;
				return Result;
			};
			OutColour = FMath::Lerp(FMath::Lerp(Linear(Row0*Width + Column0), Linear(Row0*Width + Column1), FractionX),
				FMath::Lerp(Linear(Row1*Width + Column0), Linear(Row1*Width + Column1), FractionX), FractionY);
			OutElevation = Blend(Elevation);
		}

		/** The four detail-family weights (0..1) at a position already mapped into the real map. */
		FLinearColor SampleDetail(float GroundX, float GroundY) const
		{
			if (!DetailWeights) return FLinearColor(0, 0, 0, 0);
			const int32 DetailWidth = DetailWeights->Width, DetailHeight = DetailWeights->Height;
			const float Column = FMath::Clamp((GroundX/MapSize + .5f)*DetailWidth - .5f, 0.f, DetailWidth - 1.f);
			const float Row = FMath::Clamp((.5f - GroundY/MapSize)*DetailHeight - .5f, 0.f, DetailHeight - 1.f);
			const int32 Column0 = int32(Column), Row0 = int32(Row);
			const int32 Column1 = FMath::Min(Column0 + 1, DetailWidth - 1), Row1 = FMath::Min(Row0 + 1, DetailHeight - 1);
			const float FractionX = Column - Column0, FractionY = Row - Row0;
			auto Weights = [&](int32 ColumnIndex, int32 RowIndex)
			{
				const FColor Pixel = DetailWeights->Pixels[RowIndex*DetailWidth + ColumnIndex];
				return FLinearColor(Pixel.R, Pixel.G, Pixel.B, Pixel.A)/255.f;
			};
			return FMath::Lerp(FMath::Lerp(Weights(Column0, Row0), Weights(Column1, Row0), FractionX), FMath::Lerp(Weights(Column0, Row1), Weights(Column1, Row1), FractionX), FractionY);
		}

		/** The ground at a position in raw units from the map centre: the retail terrain inside the usable square, its mirrored and warped copy outside. */
		void Evaluate(float GroundX, float GroundY, FLinearColor& OutColour, float& OutElevation, float& OutRealWeight) const
		{
			const float Distance = FMath::Max(FMath::Abs(GroundX), FMath::Abs(GroundY));
			float SampleX, SampleY, Beyond;
			MapPosition(GroundX, GroundY, SampleX, SampleY, Beyond);
			SampleMap(SampleX, SampleY, OutColour, OutElevation);
			if (Beyond > 0.f)
			{
				const float Variation = 1.f + Beyond*.18f*Noise(GroundX/(Usable*.5f), GroundY/(Usable*.5f), Seed + 5);
				OutColour.R *= Variation; OutColour.G *= Variation; OutColour.B *= Variation;
			}
			OutRealWeight = 1.f - FMath::SmoothStep(0.f, 1.f, (Distance - Usable)/(Usable*.25f));
		}

		/** Detail weights where Evaluate reads its colour. */
		FLinearColor EvaluateDetail(float GroundX, float GroundY) const
		{
			float SampleX, SampleY, Beyond;
			MapPosition(GroundX, GroundY, SampleX, SampleY, Beyond);
			return SampleDetail(SampleX, SampleY);
		}
	};

	FColor EncodeColour(const FLinearColor& Linear)
	{
		FLinearColor Safe(FMath::IsFinite(Linear.R) ? Linear.R : 0.f, FMath::IsFinite(Linear.G) ? Linear.G : 0.f, FMath::IsFinite(Linear.B) ? Linear.B : 0.f, FMath::IsFinite(Linear.A) ? Linear.A : 0.f);
		FColor Colour = Safe.ToFColorSRGB();
		// An all-black texel would read as missing data; a floor of 1 keeps real black ground distinct from an unwritten pixel.
		Colour.R = FMath::Max<uint8>(Colour.R, 1); Colour.G = FMath::Max<uint8>(Colour.G, 1); Colour.B = FMath::Max<uint8>(Colour.B, 1);
		Colour.A = uint8(FMath::Clamp(FMath::RoundToInt(Safe.A*255.f), 0, 255));
		return Colour;
	}

	bool WritePng(const FString& Path, const FSWGImage& Image)
	{
		IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Png = Wrappers.CreateImageWrapper(EImageFormat::PNG);
		return Png && Png->SetRaw(Image.Pixels.GetData(), Image.Pixels.Num()*sizeof(FColor), Image.Width, Image.Height, ERGBFormat::BGRA, 8)
			&& FFileHelper::SaveArrayToFile(Png->GetCompressed(), *Path);
	}

	bool ReadPng(const FString& Path, int32 Width, int32 Height, FSWGImage& OutImage)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return false;
		IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Png = Wrappers.CreateImageWrapper(EImageFormat::PNG);
		TArray64<uint8> Raw;
		if (!Png || !Png->SetCompressed(Bytes.GetData(), Bytes.Num()) || Png->GetWidth() != Width || Png->GetHeight() != Height || !Png->GetRaw(ERGBFormat::BGRA, 8, Raw)
			|| Raw.Num() != int64(Width)*Height*sizeof(FColor)) return false;
		OutImage.Width = Width; OutImage.Height = Height;
		OutImage.Pixels.SetNumUninitialized(Width*Height);
		FMemory::Memcpy(OutImage.Pixels.GetData(), Raw.GetData(), Raw.Num());
		return true;
	}
}

float FSWGPlanetGlobeBake::GetGroundPerRadian(float MapSize, const FSWGPlanetGlobeParams& Params)
{
	return MapSize*.5f/FMath::Max(Params.Fill*Params.CapRadius, 1e-3f);
}

FVector2D FSWGPlanetGlobeBake::GetCapUv(const FSWGPlanetGlobeParams& Params, float Theta, float Azimuth)
{
	const float Longitude = FMath::Atan2(FMath::Sin(Theta)*FMath::Sin(Azimuth), FMath::Cos(Theta));
	const float Latitude = FMath::Asin(FMath::Clamp(FMath::Sin(Theta)*FMath::Cos(Azimuth), -1.f, 1.f));
	const float HalfWidth = Params.GetCapHalfWidth();
	return FVector2D(Longitude/(2.f*HalfWidth) + .5f, .5f - Latitude/(2.f*HalfWidth));
}

FVector2D FSWGPlanetGlobeBake::GetFarUv(float Longitude, float Latitude)
{
	return FVector2D(Longitude/UE_TWO_PI + .5f, .5f - Latitude/UE_PI);
}

FColor FSWGPlanetGlobeBake::SampleNearest(const FSWGImage& Image, const FVector2D& Uv)
{
	if (!Image.IsValid()) return FColor::Black;
	const int32 Column = FMath::Clamp(int32(Uv.X*Image.Width), 0, Image.Width - 1);
	const int32 Row = FMath::Clamp(int32(Uv.Y*Image.Height), 0, Image.Height - 1);
	return Image.Pixels[Row*Image.Width + Column];
}

FString FSWGPlanetGlobeBake::MakeCacheBasePath(const FString& RealCacheBasePath, const FSWGPlanetGlobeParams& Params)
{
	return FString::Printf(TEXT("%s_globe%d_c%d_f%d_d%d"), *RealCacheBasePath, Params.CapResolution, FMath::RoundToInt(FMath::RadiansToDegrees(Params.CapRadius)*10.f), FMath::RoundToInt(Params.Fill*100.f), FSWGTerrainBake::DetailVersion);
}

void FSWGPlanetGlobeBake::Build(const FSWGTerrainBakeResult& Real, const FSWGPlanetGlobeParams& Params, FSWGPlanetGlobe& OutGlobe)
{
	OutGlobe.DetailFamilies = Real.DetailFamilies;
	FRealField Field;
	Field.Load(Real);
	Field.Seed = Params.Seed;
	const float Ground = GetGroundPerRadian(Field.MapSize, Params);
	const float CapHalfWidth = Params.GetCapHalfWidth();
	const int32 CapResolution = Params.CapResolution;
	const int32 CapCount = CapResolution*CapResolution;

	OutGlobe.CapColour.Width = OutGlobe.CapColour.Height = OutGlobe.CapNormal.Width = OutGlobe.CapNormal.Height = CapResolution;
	OutGlobe.CapColour.Pixels.SetNumUninitialized(CapCount); OutGlobe.CapNormal.Pixels.SetNumUninitialized(CapCount);
	TArray<float> Elevation; Elevation.SetNumUninitialized(CapCount);
	TArray<uint8> RealWeight; RealWeight.SetNumUninitialized(CapCount);
	ParallelFor(CapResolution, [&](int32 Row)
	{
		const float Latitude = (1.f - (Row + .5f)/CapResolution*2.f)*CapHalfWidth;
		for (int32 Column = 0; Column < CapResolution; ++Column)
		{
			const float Longitude = ((Column + .5f)/CapResolution*2.f - 1.f)*CapHalfWidth;
			FLinearColor Colour; float Height, Weight;
			Field.Evaluate(Longitude*Ground, Latitude*Ground, Colour, Height, Weight);
			const int32 Index = Row*CapResolution + Column;
			OutGlobe.CapColour.Pixels[Index] = EncodeColour(Colour);
			Elevation[Index] = FMath::IsFinite(Height) ? Height : 0.f;
			RealWeight[Index] = uint8(FMath::Clamp(FMath::RoundToInt(Weight*255.f), 0, 255));
		}
	});

	// Relief as FSWGTerrainBake shades it: two box passes first, one at the sharper cap resolutions (see FSWGTerrainBake::BakeData).
	TArray<float> Scratch; Scratch.SetNumUninitialized(CapCount);
	const int32 BlurPasses = CapResolution > FSWGTerrainBake::DefaultResolution ? 1 : 2;
	for (int32 Pass = 0; Pass < BlurPasses; ++Pass)
	{
		ParallelFor(CapResolution, [&](int32 Row)
		{
			for (int32 Column = 0; Column < CapResolution; ++Column)
			{
				float Sum = 0;
				for (int32 RowOffset = -1; RowOffset <= 1; ++RowOffset)
					for (int32 ColumnOffset = -1; ColumnOffset <= 1; ++ColumnOffset)
						Sum += Elevation[FMath::Clamp(Row + RowOffset, 0, CapResolution - 1)*CapResolution + FMath::Clamp(Column + ColumnOffset, 0, CapResolution - 1)];
				Scratch[Row*CapResolution + Column] = Sum/9.f;
			}
		});
		Swap(Elevation, Scratch);
	}
	const float Spacing = 2.f*CapHalfWidth*Ground/CapResolution;
	ParallelFor(CapResolution, [&](int32 Row)
	{
		auto ElevationAt = [&](int32 Column, int32 RowIndex) { return Elevation[FMath::Clamp(RowIndex, 0, CapResolution - 1)*CapResolution + FMath::Clamp(Column, 0, CapResolution - 1)]; };
		auto Encode = [](double Component) { return uint8(FMath::Clamp(FMath::RoundToInt((Component*.5 + .5)*255.0), 0, 255)); };
		for (int32 Column = 0; Column < CapResolution; ++Column)
		{
			const float SlopeEast = (ElevationAt(Column + 1, Row) - ElevationAt(Column - 1, Row))/(2.f*Spacing);
			// Rows run south, so the northward slope is the previous row minus the next.
			const float SlopeNorth = (ElevationAt(Column, Row - 1) - ElevationAt(Column, Row + 1))/(2.f*Spacing);
			const FVector Normal = FVector(-SlopeEast*FSWGTerrainBake::ReliefExaggeration, -SlopeNorth*FSWGTerrainBake::ReliefExaggeration, 1.f).GetSafeNormal();
			const int32 Index = Row*CapResolution + Column;
			OutGlobe.CapNormal.Pixels[Index] = FColor(Encode(Normal.X), Encode(Normal.Y), Encode(Normal.Z), RealWeight[Index]);
		}
	});

	// Detail weights over the cap's extent, through the same mirrored and warped field as the colour.
	const int32 DetailResolution = Params.GetCapDetailResolution();
	OutGlobe.CapDetail.Width = OutGlobe.CapDetail.Height = DetailResolution;
	OutGlobe.CapDetail.Pixels.SetNumUninitialized(DetailResolution*DetailResolution);
	ParallelFor(DetailResolution, [&](int32 Row)
	{
		const float Latitude = (1.f - (Row + .5f)/DetailResolution*2.f)*CapHalfWidth;
		for (int32 Column = 0; Column < DetailResolution; ++Column)
		{
			const float Longitude = ((Column + .5f)/DetailResolution*2.f - 1.f)*CapHalfWidth;
			const FLinearColor Weights = Field.EvaluateDetail(Longitude*Ground, Latitude*Ground);
			OutGlobe.CapDetail.Pixels[Row*DetailResolution + Column] = FColor(uint8(FMath::Clamp(FMath::RoundToInt(Weights.R*255.f), 0, 255)), uint8(FMath::Clamp(FMath::RoundToInt(Weights.G*255.f), 0, 255)),
				uint8(FMath::Clamp(FMath::RoundToInt(Weights.B*255.f), 0, 255)), uint8(FMath::Clamp(FMath::RoundToInt(Weights.A*255.f), 0, 255)));
		}
	});

	const int32 FarWidth = Params.GetFarWidth(), FarHeight = Params.GetFarHeight();
	OutGlobe.FarColour.Width = FarWidth; OutGlobe.FarColour.Height = FarHeight;
	OutGlobe.FarColour.Pixels.SetNumUninitialized(FarWidth*FarHeight);
	ParallelFor(FarHeight, [&](int32 Row)
	{
		const float Latitude = (.5f - (Row + .5f)/FarHeight)*UE_PI;
		for (int32 Column = 0; Column < FarWidth; ++Column)
		{
			float Longitude = ((Column + .5f)/FarWidth - .5f)*UE_TWO_PI;
			// Past +-90 degrees the ground mirrors back toward the centre, so the maps' two edges (the antipode meridian) meet without a seam.
			if (FMath::Abs(Longitude) > UE_HALF_PI) Longitude = FMath::Sign(Longitude)*(UE_PI - FMath::Abs(Longitude));
			FLinearColor Colour; float Height, Weight;
			Field.Evaluate(Longitude*Ground, Latitude*Ground, Colour, Height, Weight);
			OutGlobe.FarColour.Pixels[Row*FarWidth + Column] = EncodeColour(Colour);
		}
	});
}

bool FSWGPlanetGlobeBake::WriteCache(const FString& CacheBasePath, const FSWGPlanetGlobe& Globe)
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(CacheBasePath), true);
	FCacheHeader Header;
	Header.CapResolution = Globe.Params.CapResolution; Header.CapRadius = Globe.Params.CapRadius; Header.Fill = Globe.Params.Fill; Header.Seed = Globe.Params.Seed;
	Header.BuildSeconds = Globe.BuildSeconds;
	TArray<uint8> Bin;
	const int64 NormalBytes = Globe.CapNormal.Pixels.Num()*sizeof(FColor);
	Bin.SetNumUninitialized(sizeof(Header) + NormalBytes);
	FMemory::Memcpy(Bin.GetData(), &Header, sizeof(Header));
	FMemory::Memcpy(Bin.GetData() + sizeof(Header), Globe.CapNormal.Pixels.GetData(), NormalBytes);
	// The cap .png goes last: ReadCache needs all four files, so a half-written set is simply a miss.
	return FFileHelper::SaveArrayToFile(Bin, *(CacheBasePath + TEXT(".bin"))) && WritePng(CacheBasePath + TEXT("_far.png"), Globe.FarColour)
		&& WritePng(CacheBasePath + TEXT("_detail.png"), Globe.CapDetail) && WritePng(CacheBasePath + TEXT(".png"), Globe.CapColour);
}

bool FSWGPlanetGlobeBake::ReadCache(const FString& CacheBasePath, const FSWGPlanetGlobeParams& Params, FSWGPlanetGlobe& OutGlobe)
{
	TArray<uint8> Bin;
	if (!FFileHelper::LoadFileToArray(Bin, *(CacheBasePath + TEXT(".bin")))) return false;
	FCacheHeader Header;
	if (Bin.Num() < sizeof(Header)) return false;
	FMemory::Memcpy(&Header, Bin.GetData(), sizeof(Header));
	if (Header.Magic != CacheMagic || Header.Version != CacheVersion || Header.CapResolution != Params.CapResolution || Header.Seed != Params.Seed) return false;
	const int64 CapCount = int64(Params.CapResolution)*Params.CapResolution;
	if (Bin.Num() != sizeof(Header) + CapCount*sizeof(FColor)) return false;
	if (!ReadPng(CacheBasePath + TEXT(".png"), Params.CapResolution, Params.CapResolution, OutGlobe.CapColour)
		|| !ReadPng(CacheBasePath + TEXT("_far.png"), Params.GetFarWidth(), Params.GetFarHeight(), OutGlobe.FarColour)
		|| !ReadPng(CacheBasePath + TEXT("_detail.png"), Params.GetCapDetailResolution(), Params.GetCapDetailResolution(), OutGlobe.CapDetail)) return false;
	OutGlobe.CapNormal.Width = OutGlobe.CapNormal.Height = Params.CapResolution;
	OutGlobe.CapNormal.Pixels.SetNumUninitialized(CapCount);
	FMemory::Memcpy(OutGlobe.CapNormal.Pixels.GetData(), Bin.GetData() + sizeof(Header), CapCount*sizeof(FColor));
	OutGlobe.BuildSeconds = Header.BuildSeconds;
	return true;
}

bool FSWGPlanetGlobeBake::LoadOrBuild(const FSWGTerrainBakeResult& Real, const FSWGPlanetGlobeParams& Params, FSWGPlanetGlobe& OutGlobe)
{
	const double LoadStart = FPlatformTime::Seconds();
	OutGlobe = FSWGPlanetGlobe();
	OutGlobe.Params = Params;
	OutGlobe.Params.CapResolution = FMath::Clamp(Params.CapResolution, 64, 4096);
	OutGlobe.Params.CapRadius = FMath::Clamp(Params.CapRadius, .05f, 1.2f);
	OutGlobe.Params.Fill = FMath::Clamp(Params.Fill, .25f, 4.f);
	if (!Real.Colour.IsValid()) return false;
	const FString CacheBasePath = Real.CacheBasePath.IsEmpty() ? FString() : MakeCacheBasePath(Real.CacheBasePath, OutGlobe.Params);
	if (!CacheBasePath.IsEmpty())
	{
		FSWGPlanetGlobe Cached;
		Cached.Params = OutGlobe.Params;
		if (ReadCache(CacheBasePath, OutGlobe.Params, Cached))
		{
			OutGlobe = MoveTemp(Cached);
			OutGlobe.DetailFamilies = Real.DetailFamilies;
			OutGlobe.bFromCache = true; OutGlobe.CacheBasePath = CacheBasePath; OutGlobe.LoadSeconds = FPlatformTime::Seconds() - LoadStart;
			UE_LOG(LogTemp, Log, TEXT("SWGPlanetGlobe: %s read from cache in %.2f s (built once in %.2f s)"), *CacheBasePath, OutGlobe.LoadSeconds, OutGlobe.BuildSeconds);
			return true;
		}
	}
	const double BuildStart = FPlatformTime::Seconds();
	Build(Real, OutGlobe.Params, OutGlobe);
	OutGlobe.BuildSeconds = FPlatformTime::Seconds() - BuildStart;
	const double WriteStart = FPlatformTime::Seconds();
	if (!CacheBasePath.IsEmpty())
	{
		OutGlobe.CacheBasePath = CacheBasePath;
		if (!WriteCache(CacheBasePath, OutGlobe)) UE_LOG(LogTemp, Warning, TEXT("SWGPlanetGlobe: could not write cache %s"), *CacheBasePath);
	}
	OutGlobe.LoadSeconds = FPlatformTime::Seconds() - LoadStart;
	UE_LOG(LogTemp, Log, TEXT("SWGPlanetGlobe: built %s at cap %d, cap radius %.1f deg, fill %.2f in %.2f s (cache write %.2f s)"), *CacheBasePath, OutGlobe.Params.CapResolution,
		FMath::RadiansToDegrees(OutGlobe.Params.CapRadius), OutGlobe.Params.Fill, OutGlobe.BuildSeconds, FPlatformTime::Seconds() - WriteStart);
	return true;
}
