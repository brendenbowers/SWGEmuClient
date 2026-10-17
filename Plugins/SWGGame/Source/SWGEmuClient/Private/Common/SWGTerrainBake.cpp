#include "Common/SWGTerrainBake.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGTerrainEvaluator.h"
#include "TRE/SWGTerrainTint.h"
#include "TRE/SWGShaderReader.h"
#include "Async/ParallelFor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

namespace
{
	constexpr uint32 CacheMagic = 0x42435053; // "SPCB"
	constexpr uint32 CacheVersion = 6;

	struct FCacheHeader
	{
		uint32 Magic = CacheMagic;
		uint32 Version = CacheVersion;
		int32 Resolution = 0;
		float MapSize = 0;
		float MinHeight = 0;
		float MaxHeight = 0;
		float Average[4] = {};
		double BakeSeconds = 0;
	};

	FCriticalSection AverageLock;
	TMap<FString, TOptional<FLinearColor>> AverageColours;

	/** Stand-in colour for a family or water whose texture cannot be read. */
	const FLinearColor UnknownColour(.5f, .5f, .5f);

	FLinearColor MeanLinear(const FSWGImage& Image)
	{
		double Red = 0, Green = 0, Blue = 0;
		int64 Count = 0;
		for (int32 Row = 0; Row < Image.Height; Row += 2)
		{
			for (int32 Column = 0; Column < Image.Width; Column += 2, ++Count)
			{
				const FLinearColor Pixel = FLinearColor::FromSRGBColor(Image.Pixels[Row*Image.Width + Column]);
				Red += Pixel.R; Green += Pixel.G; Blue += Pixel.B;
			}
		}
		return Count ? FLinearColor(Red/Count, Green/Count, Blue/Count) : UnknownColour;
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

	/** One line per family: id, diffuse path, linear average. */
	FString FamiliesToText(const TArray<FSWGTerrainDetailFamily>& Families)
	{
		FString Text;
		for (const FSWGTerrainDetailFamily& Family : Families)
			Text += FString::Printf(TEXT("%d\t%s\t%.6f\t%.6f\t%.6f\n"), Family.FamilyId, *Family.DiffusePath, Family.Average.R, Family.Average.G, Family.Average.B);
		return Text;
	}

	bool FamiliesFromText(const FString& Text, TArray<FSWGTerrainDetailFamily>& OutFamilies)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TArray<FString> Fields;
			Line.ParseIntoArray(Fields, TEXT("\t"), false);
			if (Fields.Num() != 5) return false;
			FSWGTerrainDetailFamily& Family = OutFamilies.AddDefaulted_GetRef();
			Family.FamilyId = FCString::Atoi(*Fields[0]); Family.DiffusePath = Fields[1];
			Family.Average = FLinearColor(FCString::Atof(*Fields[2]), FCString::Atof(*Fields[3]), FCString::Atof(*Fields[4]));
		}
		return OutFamilies.Num() <= FSWGTerrainBake::DetailFamilyCount;
	}
}

FString FSWGTerrainBake::FindGroundTerrain(const USWGTreSubsystem& Tre, const FString& PlanetAppearance)
{
	FString Name = FPaths::GetBaseFilename(PlanetAppearance).ToLower();
	if (!Name.RemoveFromStart(TEXT("planet_")) || Name.IsEmpty()) return FString();
	const FString Terrain = FString::Printf(TEXT("terrain/%s.trn"), *Name);
	return Tre.FileExists(Terrain) ? Terrain : FString();
}

FString FSWGTerrainBake::ResolveDiffusePath(const USWGTreSubsystem& Tre, const FString& LayerOrShader)
{
	const FString ShaderPath = LayerOrShader.EndsWith(TEXT(".sht")) ? LayerOrShader : FString::Printf(TEXT("shader/%s.sht"), *FPaths::GetBaseFilename(LayerOrShader));
	if (Tre.FileExists(ShaderPath))
	{
		FSWGShaderData Shader;
		if (FSWGShaderReader::ReadShader(Tre.CreateIffReader(ShaderPath), Shader))
			if (const FSWGShaderTexture* Texture = Shader.FindTexture(ESWGShaderTextureUsage::Diffuse)) return Texture->VirtualPath;
	}
	return FString::Printf(TEXT("texture/%s.dds"), *FPaths::GetBaseFilename(LayerOrShader));
}

bool FSWGTerrainBake::GetAverageColour(const USWGTreSubsystem& Tre, const FString& TexturePath, FLinearColor& OutColour)
{
	{
		FScopeLock Lock(&AverageLock);
		if (const TOptional<FLinearColor>* Known = AverageColours.Find(TexturePath))
		{
			if (Known->IsSet()) OutColour = Known->GetValue();
			return Known->IsSet();
		}
	}
	TOptional<FLinearColor> Average;
	FSWGImage Image;
	if (Tre.FileExists(TexturePath) && FSWGDdsDecoder::Decode(Tre.ExtractFile(TexturePath), Image) && Image.IsValid()) Average = MeanLinear(Image);
	FScopeLock Lock(&AverageLock);
	AverageColours.Add(TexturePath, Average);
	if (Average.IsSet()) OutColour = Average.GetValue();
	return Average.IsSet();
}

FString FSWGTerrainBake::MakeCacheBasePath(const FString& CacheDirectory, const FString& TerrainPath, const TArray<uint8>& TerrainBytes, int32 Resolution)
{
	FMD5 Hash;
	Hash.Update(TerrainBytes.GetData(), TerrainBytes.Num());
	uint8 Digest[16];
	Hash.Final(Digest);
	return CacheDirectory / FString::Printf(TEXT("%s_%s_%d_d%d"), *FPaths::GetBaseFilename(TerrainPath), *BytesToHex(Digest, 8), Resolution, DetailVersion);
}

bool FSWGTerrainBake::ParseGround(const USWGTreSubsystem& Tre, TArray<uint8> TerrainBytes, FSWGTerrainData& OutData)
{
	if (!FSWGTerrainReader::ReadTerrain(FSWGIffReader(MoveTemp(TerrainBytes)), OutData)) return false;
	// Colour-ramp images for the ACRH/ACRF affectors, as USWGTerrainSubsystem::LoadTerrain loads them (a shared loader is later work).
	TArray<FString> RampPaths;
	SWGTerrainTint::CollectRampPaths(OutData, RampPaths);
	for (const FString& RampPath : RampPaths)
	{
		FSWGTerrainColorRamp Ramp;
		if (Tre.FileExists(RampPath) && SWGTerrainTint::DecodeTga(Tre.ExtractFile(RampPath), Ramp)) OutData.ColorRamps.Add(RampPath, MoveTemp(Ramp));
	}
	return true;
}

void FSWGTerrainBake::BakeData(const USWGTreSubsystem& Tre, const FSWGTerrainData& Data, int32 Resolution, FSWGTerrainBakeResult& OutResult, float WindowSize)
{
	TMap<int32, FLinearColor> FamilyColours;
	for (const FSWGShaderFamily& Family : Data.ShaderFamilies)
	{
		FLinearColor Colour = UnknownColour;
		if (!Family.LayerNames.IsEmpty()) GetAverageColour(Tre, ResolveDiffusePath(Tre, Family.LayerNames[0]), Colour);
		FamilyColours.Add(Family.FamilyId, Colour);
	}
	FLinearColor WaterColour(.04f, .12f, .2f);
	if (!Data.Header.GlobalWaterTableShader.IsEmpty())
	{
		const FString WaterTexture = ResolveDiffusePath(Tre, Data.Header.GlobalWaterTableShader);
		const bool bKnown = GetAverageColour(Tre, WaterTexture, WaterColour);
		UE_LOG(LogTemp, Log, TEXT("SWGTerrainBake: water %s -> %s (%s) = (%.2f %.2f %.2f), global table %d at %.1f"), *Data.Header.GlobalWaterTableShader, *WaterTexture,
			bKnown ? TEXT("read") : TEXT("missing"), WaterColour.R, WaterColour.G, WaterColour.B, Data.Header.bUseGlobalWaterTable ? 1 : 0, Data.Header.GlobalWaterTableHeight);
	}

	const float MapSize = WindowSize > 0 ? WindowSize : Data.Header.MapSize, HalfMap = MapSize*.5f;
	const int32 PixelCount = Resolution*Resolution;

	// The families that cover the most land get detail textures; a coarse grid is enough to rank them.
	{
		constexpr int32 SurveyResolution = 128;
		TMap<int32, double> Coverage;
		for (int32 Row = 0; Row < SurveyResolution; ++Row)
			for (int32 Column = 0; Column < SurveyResolution; ++Column)
			{
				const float RawX = (Column + .5f)/SurveyResolution*MapSize - HalfMap;
				const float RawY = HalfMap - (Row + .5f)/SurveyResolution*MapSize;
				FSWGTerrainGroundSample Ground;
				const float Height = FSWGTerrainEvaluator::GetHeightAndGround(Data, RawX, RawY, Ground);
				float WaterHeight = 0;
				if (FSWGTerrainEvaluator::GetWaterHeight(Data, RawX, RawY, WaterHeight) && Height < WaterHeight) continue;
				TMap<int32, float> Weights;
				FSWGTerrainEvaluator::GetShaderWeights(Data, RawX, RawY, Weights);
				for (const TPair<int32, float>& Weight : Weights) Coverage.FindOrAdd(Weight.Key) += Weight.Value;
			}
		Coverage.ValueSort([](double First, double Second) { return First > Second; });
		for (const TPair<int32, double>& Entry : Coverage)
		{
			if (OutResult.DetailFamilies.Num() >= DetailFamilyCount) break;
			const FSWGShaderFamily* Family = Data.ShaderFamilies.FindByPredicate([&](const FSWGShaderFamily& Candidate) { return Candidate.FamilyId == Entry.Key; });
			if (!Family || Family->LayerNames.IsEmpty()) continue;
			FSWGTerrainDetailFamily Detail;
			Detail.FamilyId = Entry.Key; Detail.DiffusePath = ResolveDiffusePath(Tre, Family->LayerNames[0]);
			if (GetAverageColour(Tre, Detail.DiffusePath, Detail.Average)) OutResult.DetailFamilies.Add(Detail);
		}
	}

	TArray<float> Heights; Heights.SetNumUninitialized(PixelCount);
	OutResult.Colour.Width = OutResult.Colour.Height = Resolution;
	OutResult.Colour.Pixels.SetNumUninitialized(PixelCount);
	// Per-pixel weights of the detail families at full resolution; halved into DetailWeights below.
	TArray<FColor> DetailFull; DetailFull.SetNumZeroed(PixelCount);
	TArray<FVector3d> RowSums; RowSums.SetNumZeroed(Resolution);
	// Rows in parallel: the evaluator only reads the immutable terrain data.
	ParallelFor(Resolution, [&](int32 Row)
	{
		FVector3d RowSum = FVector3d::ZeroVector;
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			const float RawX = (Column + .5f)/Resolution*MapSize - HalfMap;
			const float RawY = HalfMap - (Row + .5f)/Resolution*MapSize;
			FSWGTerrainGroundSample Ground;
			float Height = FSWGTerrainEvaluator::GetHeightAndGround(Data, RawX, RawY, Ground);
			float WaterHeight = 0;
			const int32 Index = Row*Resolution + Column;
			if (!FMath::IsFinite(Height)) Height = 0;
			FLinearColor Linear;
			uint8 WaterFlag = 0;
			if (FSWGTerrainEvaluator::GetWaterHeight(Data, RawX, RawY, WaterHeight) && Height < WaterHeight)
			{
				Heights[Index] = WaterHeight; Linear = WaterColour; WaterFlag = 255;
			}
			else
			{
				Heights[Index] = Height;
				TMap<int32, float> Weights;
				FSWGTerrainEvaluator::GetShaderWeights(Data, RawX, RawY, Weights);
				int32 Dominant = 0; float Strongest = 0;
				for (const TPair<int32, float>& Weight : Weights) if (Weight.Value > Strongest) { Dominant = Weight.Key; Strongest = Weight.Value; }
				const FLinearColor* Family = FamilyColours.Find(Dominant);
				const FLinearColor Tint = FLinearColor::FromSRGBColor(Ground.Tint);
				const FLinearColor Texture = Family ? *Family : UnknownColour;
				Linear = FLinearColor(Tint.R*Texture.R, Tint.G*Texture.G, Tint.B*Texture.B);
				uint8 Slots[DetailFamilyCount] = {};
				for (int32 Slot = 0; Slot < OutResult.DetailFamilies.Num(); ++Slot)
					if (const float* Weight = Weights.Find(OutResult.DetailFamilies[Slot].FamilyId)) Slots[Slot] = uint8(FMath::Clamp(FMath::RoundToInt(*Weight*255.f), 0, 255));
				DetailFull[Index] = FColor(Slots[0], Slots[1], Slots[2], Slots[3]);
			}
			RowSum += FVector3d(Linear.R, Linear.G, Linear.B);
			OutResult.Colour.Pixels[Index] = Linear.ToFColorSRGB();
			OutResult.Colour.Pixels[Index].A = WaterFlag;
		}
		RowSums[Row] = RowSum;
	});

	FVector3d ColourSum = FVector3d::ZeroVector;
	for (const FVector3d& RowSum : RowSums) ColourSum += RowSum;
	OutResult.AverageColour = FLinearColor(ColourSum.X/PixelCount, ColourSum.Y/PixelCount, ColourSum.Z/PixelCount);
	OutResult.MinHeight = FMath::Min(Heights); OutResult.MaxHeight = FMath::Max(Heights);
	OutResult.MapSize = MapSize;

	const int32 DetailResolution = GetDetailResolution(Resolution);
	OutResult.DetailWeights.Width = OutResult.DetailWeights.Height = DetailResolution;
	OutResult.DetailWeights.Pixels.SetNumUninitialized(DetailResolution*DetailResolution);
	const int32 DetailStep = FMath::Max(Resolution/DetailResolution, 1);
	ParallelFor(DetailResolution, [&](int32 Row)
	{
		for (int32 Column = 0; Column < DetailResolution; ++Column)
		{
			int32 Sum[4] = {};
			for (int32 RowOffset = 0; RowOffset < DetailStep; ++RowOffset)
				for (int32 ColumnOffset = 0; ColumnOffset < DetailStep; ++ColumnOffset)
				{
					const FColor& Source = DetailFull[FMath::Min(Row*DetailStep + RowOffset, Resolution - 1)*Resolution + FMath::Min(Column*DetailStep + ColumnOffset, Resolution - 1)];
					Sum[0] += Source.R; Sum[1] += Source.G; Sum[2] += Source.B; Sum[3] += Source.A;
				}
			const int32 Count = DetailStep*DetailStep;
			OutResult.DetailWeights.Pixels[Row*DetailResolution + Column] = FColor((Sum[0] + Count/2)/Count, (Sum[1] + Count/2)/Count, (Sum[2] + Count/2)/Count, (Sum[3] + Count/2)/Count);
		}
	});
	DetailFull.Empty();

	OutResult.Heights.SetNumUninitialized(PixelCount);
	const float HeightRange = FMath::Max(OutResult.MaxHeight - OutResult.MinHeight, 1e-3f);
	for (int32 Index = 0; Index < PixelCount; ++Index)
		OutResult.Heights[Index] = uint16(FMath::Clamp(FMath::RoundToInt((Heights[Index] - OutResult.MinHeight)/HeightRange*65535.f), 0, 65535));

	OutResult.Normal.Width = OutResult.Normal.Height = Resolution;
	OutResult.Normal.Pixels.SetNumUninitialized(PixelCount);
	// The fractal height noise is finer than the shading can use, so only its shape shades: two 3x3 box passes up to 1024, one at the sharper
	// resolutions (4 m a pixel), which keeps the relief detail the extra pixels bought.
	TArray<float> Scratch; Scratch.SetNumUninitialized(PixelCount);
	const int32 BlurPasses = Resolution > DefaultResolution ? 1 : 2;
	for (int32 Pass = 0; Pass < BlurPasses; ++Pass)
	{
		ParallelFor(Resolution, [&](int32 Row)
		{
			for (int32 Column = 0; Column < Resolution; ++Column)
			{
				float Sum = 0;
				for (int32 RowOffset = -1; RowOffset <= 1; ++RowOffset)
					for (int32 ColumnOffset = -1; ColumnOffset <= 1; ++ColumnOffset)
						Sum += Heights[FMath::Clamp(Row + RowOffset, 0, Resolution - 1)*Resolution + FMath::Clamp(Column + ColumnOffset, 0, Resolution - 1)];
				Scratch[Row*Resolution + Column] = Sum/9.f;
			}
		});
		Swap(Heights, Scratch);
	}
	const float Spacing = MapSize/Resolution;
	ParallelFor(Resolution, [&](int32 Row)
	{
		auto HeightAt = [&](int32 Column, int32 RowIndex) { return Heights[FMath::Clamp(RowIndex, 0, Resolution - 1)*Resolution + FMath::Clamp(Column, 0, Resolution - 1)]; };
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			const float SlopeEast = (HeightAt(Column + 1, Row) - HeightAt(Column - 1, Row))/(2.f*Spacing);
			// Rows run south, so the northward slope is the previous row minus the next.
			const float SlopeNorth = (HeightAt(Column, Row - 1) - HeightAt(Column, Row + 1))/(2.f*Spacing);
			// The map's outer rim ramps up steeply; shade it flat so it does not draw a frame around the region.
			const float RimFade = FMath::SmoothStep(0.f, 1.f, FMath::Min(FMath::Min(Column, Resolution - 1 - Column), FMath::Min(Row, Resolution - 1 - Row))/(Resolution*.04f));
			const FVector Normal = FVector(-SlopeEast*ReliefExaggeration*RimFade, -SlopeNorth*ReliefExaggeration*RimFade, 1.f).GetSafeNormal();
			auto Encode = [](double Component) { return uint8(FMath::Clamp(FMath::RoundToInt((Component*.5 + .5)*255.0), 0, 255)); };
			OutResult.Normal.Pixels[Row*Resolution + Column] = FColor(Encode(Normal.X), Encode(Normal.Y), Encode(Normal.Z), 255);
		}
	});
}

bool FSWGTerrainBake::WriteCache(const FString& CacheBasePath, const FSWGTerrainBakeResult& Result)
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(CacheBasePath), true);
	IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Png = Wrappers.CreateImageWrapper(EImageFormat::PNG);
	if (!Png || !Png->SetRaw(Result.Colour.Pixels.GetData(), Result.Colour.Pixels.Num()*sizeof(FColor), Result.Colour.Width, Result.Colour.Height, ERGBFormat::BGRA, 8)) return false;

	FCacheHeader Header;
	Header.Resolution = Result.Colour.Width; Header.MapSize = Result.MapSize; Header.MinHeight = Result.MinHeight; Header.MaxHeight = Result.MaxHeight;
	Header.Average[0] = Result.AverageColour.R; Header.Average[1] = Result.AverageColour.G; Header.Average[2] = Result.AverageColour.B;
	Header.BakeSeconds = Result.BakeSeconds;
	TArray<uint8> Bin;
	const int64 NormalBytes = Result.Normal.Pixels.Num()*sizeof(FColor), HeightBytes = Result.Heights.Num()*sizeof(uint16);
	Bin.SetNumUninitialized(sizeof(Header) + NormalBytes + HeightBytes);
	FMemory::Memcpy(Bin.GetData(), &Header, sizeof(Header));
	FMemory::Memcpy(Bin.GetData() + sizeof(Header), Result.Normal.Pixels.GetData(), NormalBytes);
	FMemory::Memcpy(Bin.GetData() + sizeof(Header) + NormalBytes, Result.Heights.GetData(), HeightBytes);
	// The colour .png goes last: ReadCache needs all four files, so a half-written set is simply a miss.
	return FFileHelper::SaveArrayToFile(Bin, *(CacheBasePath + TEXT(".bin"))) && WritePng(CacheBasePath + TEXT("_detail.png"), Result.DetailWeights)
		&& FFileHelper::SaveStringToFile(FamiliesToText(Result.DetailFamilies), *(CacheBasePath + TEXT("_detail.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		&& FFileHelper::SaveArrayToFile(Png->GetCompressed(), *(CacheBasePath + TEXT(".png")));
}

bool FSWGTerrainBake::ReadCache(const FString& CacheBasePath, FSWGTerrainBakeResult& OutResult)
{
	TArray<uint8> Bin, PngBytes;
	if (!FFileHelper::LoadFileToArray(Bin, *(CacheBasePath + TEXT(".bin"))) || !FFileHelper::LoadFileToArray(PngBytes, *(CacheBasePath + TEXT(".png")))) return false;
	FCacheHeader Header;
	if (Bin.Num() < sizeof(Header)) return false;
	FMemory::Memcpy(&Header, Bin.GetData(), sizeof(Header));
	if (Header.Magic != CacheMagic || Header.Version != CacheVersion || Header.Resolution <= 0) return false;
	const int64 PixelCount = int64(Header.Resolution)*Header.Resolution;
	const int64 NormalBytes = PixelCount*sizeof(FColor), HeightBytes = PixelCount*sizeof(uint16);
	if (Bin.Num() != sizeof(Header) + NormalBytes + HeightBytes) return false;

	IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Png = Wrappers.CreateImageWrapper(EImageFormat::PNG);
	TArray64<uint8> Raw;
	if (!Png || !Png->SetCompressed(PngBytes.GetData(), PngBytes.Num()) || Png->GetWidth() != Header.Resolution || Png->GetHeight() != Header.Resolution
		|| !Png->GetRaw(ERGBFormat::BGRA, 8, Raw) || Raw.Num() != NormalBytes) return false;
	FString FamilyText;
	if (!ReadPng(CacheBasePath + TEXT("_detail.png"), GetDetailResolution(Header.Resolution), GetDetailResolution(Header.Resolution), OutResult.DetailWeights)
		|| !FFileHelper::LoadFileToString(FamilyText, *(CacheBasePath + TEXT("_detail.txt"))) || !FamiliesFromText(FamilyText, OutResult.DetailFamilies)) return false;

	OutResult.Colour.Width = OutResult.Colour.Height = OutResult.Normal.Width = OutResult.Normal.Height = Header.Resolution;
	OutResult.Colour.Pixels.SetNumUninitialized(PixelCount); OutResult.Normal.Pixels.SetNumUninitialized(PixelCount); OutResult.Heights.SetNumUninitialized(PixelCount);
	FMemory::Memcpy(OutResult.Colour.Pixels.GetData(), Raw.GetData(), NormalBytes);
	FMemory::Memcpy(OutResult.Normal.Pixels.GetData(), Bin.GetData() + sizeof(Header), NormalBytes);
	FMemory::Memcpy(OutResult.Heights.GetData(), Bin.GetData() + sizeof(Header) + NormalBytes, HeightBytes);
	OutResult.MapSize = Header.MapSize; OutResult.MinHeight = Header.MinHeight; OutResult.MaxHeight = Header.MaxHeight;
	OutResult.AverageColour = FLinearColor(Header.Average[0], Header.Average[1], Header.Average[2]);
	OutResult.BakeSeconds = Header.BakeSeconds;
	OutResult.CacheBasePath = CacheBasePath;
	return true;
}

bool FSWGTerrainBake::LoadOrBake(const USWGTreSubsystem& Tre, const FString& TerrainPath, int32 Resolution, const FString& CacheDirectory, FSWGTerrainBakeResult& OutResult)
{
	const double LoadStart = FPlatformTime::Seconds();
	const TArray<uint8> TerrainBytes = Tre.ExtractFile(TerrainPath);
	if (TerrainBytes.IsEmpty()) return false;
	const FString CacheBasePath = MakeCacheBasePath(CacheDirectory, TerrainPath, TerrainBytes, Resolution);
	OutResult = FSWGTerrainBakeResult();
	if (ReadCache(CacheBasePath, OutResult))
	{
		OutResult.bFromCache = true; OutResult.LoadSeconds = FPlatformTime::Seconds() - LoadStart;
		UE_LOG(LogTemp, Log, TEXT("SWGTerrainBake: %s read from cache %s in %.2f s (baked once in %.2f s)"), *TerrainPath, *CacheBasePath, OutResult.LoadSeconds, OutResult.BakeSeconds);
		return true;
	}
	OutResult = FSWGTerrainBakeResult();
	FSWGTerrainData Data;
	if (!ParseGround(Tre, TerrainBytes, Data)) return false;
	const double BakeStart = FPlatformTime::Seconds();
	BakeData(Tre, Data, Resolution, OutResult);
	OutResult.BakeSeconds = FPlatformTime::Seconds() - BakeStart;
	OutResult.CacheBasePath = CacheBasePath;
	if (!WriteCache(CacheBasePath, OutResult)) UE_LOG(LogTemp, Warning, TEXT("SWGTerrainBake: could not write cache %s"), *CacheBasePath);
	OutResult.LoadSeconds = FPlatformTime::Seconds() - LoadStart;
	UE_LOG(LogTemp, Log, TEXT("SWGTerrainBake: baked %s at %dx%d in %.2f s (parse %.2f s), heights %.0f..%.0f, cache %s"), *TerrainPath, Resolution, Resolution,
		OutResult.BakeSeconds, BakeStart - LoadStart, OutResult.MinHeight, OutResult.MaxHeight, *CacheBasePath);
	return true;
}

bool FSWGTerrainBake::LoadFallbackMap(const USWGTreSubsystem& Tre, const FString& TerrainPath, FSWGTerrainBakeResult& OutResult)
{
	const FString MapPath = FString::Printf(TEXT("texture/ui_map_%s.dds"), *FPaths::GetBaseFilename(TerrainPath));
	OutResult = FSWGTerrainBakeResult();
	if (!Tre.FileExists(MapPath) || !FSWGDdsDecoder::Decode(Tre.ExtractFile(MapPath), OutResult.Colour) || !OutResult.Colour.IsValid()) return false;
	OutResult.AverageColour = MeanLinear(OutResult.Colour);
	OutResult.Normal.Width = OutResult.Normal.Height = 1;
	OutResult.Normal.Pixels = {FColor(128, 128, 255, 255)};
	OutResult.bFallbackMap = true;
	return true;
}
