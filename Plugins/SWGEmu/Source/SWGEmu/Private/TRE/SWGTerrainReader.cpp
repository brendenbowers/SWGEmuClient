#include "TRE/SWGTerrainReader.h"

namespace
{
	float ReadFloatLE(const uint8* Data, int32 Offset)
	{
		float Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(float));
		return Value;
	}

	uint32 ReadUInt32LE(const uint8* Data, int32 Offset)
	{
		uint32 Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(uint32));
		return Value;
	}
}

bool FSWGTerrainReader::FindChildForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& FormType, FSWGIffChunk& OutChunk)
{
	for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
	{
		if (Child.IsForm() && Child.FormType == FormType)
		{
			OutChunk = Child;
			return true;
		}
	}
	return false;
}

TArray<FSWGIffChunk> FSWGTerrainReader::FindChildForms(const FSWGIffReader& Reader, const FSWGIffChunk& Parent)
{
	TArray<FSWGIffChunk> Result;
	for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
	{
		if (Child.IsForm())
			Result.Add(Child);
	}
	return Result;
}

bool FSWGTerrainReader::FindChildChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& Tag, FSWGIffChunk& OutChunk)
{
	for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
	{
		if (!Child.IsForm() && Child.Tag == Tag)
		{
			OutChunk = Child;
			return true;
		}
	}
	return false;
}

FString FSWGTerrainReader::ReadNullTerminatedStringAt(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, int32 Offset)
{
	const uint8* Data = Reader.GetChunkData(Chunk);
	const int32 Len = FMath::Max(0, Chunk.DataSize - Offset - 1);
	return FString::ConstructFromPtrSize((const ANSICHAR*)(Data + Offset), Len);
}

bool FSWGTerrainReader::ReadInformationHeader(const FSWGIffReader& Reader, const FSWGIffChunk& IhdrForm, FString& OutName, bool& bOutEnabled)
{
	FSWGIffChunk F0001, DataChunk;
	if (!FindChildForm(Reader, IhdrForm, TEXT("0001"), F0001)) return false;
	if (!FindChildChunk(Reader, F0001, TEXT("DATA"), DataChunk)) return false;

	const uint8* Data = Reader.GetChunkData(DataChunk);
	bOutEnabled = ReadUInt32LE(Data, 0) != 0;
	OutName = ReadNullTerminatedStringAt(Reader, DataChunk, 4);
	return true;
}

bool FSWGTerrainReader::ReadBoundaryCircle(const FSWGIffReader& Reader, const FSWGIffChunk& BcirForm, FSWGTerrainBoundary& OutBoundary)
{
	FSWGIffChunk Version, Ihdr, DataChunk;
	if (!FindChildForm(Reader, BcirForm, TEXT("0002"), Version)) return false;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutBoundary.bEnabled);

	if (!FindChildChunk(Reader, Version, TEXT("DATA"), DataChunk)) return false;

	const uint8* D = Reader.GetChunkData(DataChunk);
	OutBoundary.Type = ESWGTerrainBoundaryType::Circle;
	OutBoundary.CenterX = ReadFloatLE(D, 0);
	OutBoundary.CenterY = ReadFloatLE(D, 4);
	OutBoundary.Radius = ReadFloatLE(D, 8);
	OutBoundary.FeatheringType = (int32)ReadUInt32LE(D, 12);
	OutBoundary.FeatheringAmount = FMath::Clamp(ReadFloatLE(D, 16), 0.0f, 1.0f);
	return true;
}

bool FSWGTerrainReader::ReadBoundaryRectangle(const FSWGIffReader& Reader, const FSWGIffChunk& BrecForm, FSWGTerrainBoundary& OutBoundary)
{
	FSWGIffChunk Version;
	const bool bV3 = FindChildForm(Reader, BrecForm, TEXT("0003"), Version);
	if (!bV3 && !FindChildForm(Reader, BrecForm, TEXT("0002"), Version)) return false;

	FSWGIffChunk Ihdr, DataChunk;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutBoundary.bEnabled);

	if (!FindChildChunk(Reader, Version, TEXT("DATA"), DataChunk)) return false;

	const uint8* D = Reader.GetChunkData(DataChunk);
	OutBoundary.Type = ESWGTerrainBoundaryType::Rectangle;
	OutBoundary.X0 = ReadFloatLE(D, 0);
	OutBoundary.Y0 = ReadFloatLE(D, 4);
	OutBoundary.X1 = ReadFloatLE(D, 8);
	OutBoundary.Y1 = ReadFloatLE(D, 12);
	OutBoundary.FeatheringType = (int32)ReadUInt32LE(D, 16);
	OutBoundary.FeatheringAmount = FMath::Clamp(ReadFloatLE(D, 20), 0.0f, 1.0f);

	// v0002 ends here (24 bytes); v0003 adds water-table fields (var7 and shaderSize/shaderName ignored).
	if (bV3 && DataChunk.DataSize >= 36)
	{
		OutBoundary.bLocalWaterTableEnabled = ReadUInt32LE(D, 24) != 0;
		OutBoundary.LocalWaterTableHeight = ReadFloatLE(D, 32);
	}

	return true;
}

bool FSWGTerrainReader::ReadBoundaryPolygon(const FSWGIffReader& Reader, const FSWGIffChunk& BpolForm, FSWGTerrainBoundary& OutBoundary)
{
	FSWGIffChunk Version, Ihdr, DataChunk;
	if (!FindChildForm(Reader, BpolForm, TEXT("0005"), Version)) return false;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutBoundary.bEnabled);

	if (!FindChildChunk(Reader, Version, TEXT("DATA"), DataChunk)) return false;

	const uint8* D = Reader.GetChunkData(DataChunk);
	OutBoundary.Type = ESWGTerrainBoundaryType::Polygon;

	const int32 Count = (int32)ReadUInt32LE(D, 0);
	int32 Offset = 4;
	OutBoundary.Vertices.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const float X = ReadFloatLE(D, Offset);
		const float Y = ReadFloatLE(D, Offset + 4);
		OutBoundary.Vertices.Add(FVector2D(X, Y));
		Offset += 8;
	}

	OutBoundary.FeatheringType = (int32)ReadUInt32LE(D, Offset);
	OutBoundary.FeatheringAmount = FMath::Clamp(ReadFloatLE(D, Offset + 4), 0.0f, 1.0f);
	OutBoundary.bLocalWaterTableEnabled = ReadUInt32LE(D, Offset + 8) != 0;
	OutBoundary.LocalWaterTableHeight = ReadFloatLE(D, Offset + 12);
	// shaderSize + shaderName follow, not needed for height evaluation.

	return true;
}

bool FSWGTerrainReader::ReadBoundary(const FSWGIffReader& Reader, const FSWGIffChunk& BoundaryForm, FSWGTerrainBoundary& OutBoundary)
{
	if (BoundaryForm.FormType == TEXT("BCIR")) return ReadBoundaryCircle(Reader, BoundaryForm, OutBoundary);
	if (BoundaryForm.FormType == TEXT("BREC")) return ReadBoundaryRectangle(Reader, BoundaryForm, OutBoundary);
	if (BoundaryForm.FormType == TEXT("BPOL")) return ReadBoundaryPolygon(Reader, BoundaryForm, OutBoundary);
	return false;
}

bool FSWGTerrainReader::ReadAffectorHeightConstant(const FSWGIffReader& Reader, const FSWGIffChunk& AhcnForm, FSWGTerrainAffector& OutAffector)
{
	FSWGIffChunk Version, Ihdr, DataChunk;
	if (!FindChildForm(Reader, AhcnForm, TEXT("0000"), Version)) return false;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutAffector.bEnabled);

	if (!FindChildChunk(Reader, Version, TEXT("DATA"), DataChunk)) return false;

	const uint8* D = Reader.GetChunkData(DataChunk);
	OutAffector.Type = ESWGTerrainAffectorType::HeightConstant;
	OutAffector.OperationType = (int32)ReadUInt32LE(D, 0);
	OutAffector.Height = ReadFloatLE(D, 4);
	return true;
}

bool FSWGTerrainReader::ReadAffectorHeightFractal(const FSWGIffReader& Reader, const FSWGIffChunk& AhfrForm, FSWGTerrainAffector& OutAffector)
{
	FSWGIffChunk Version, Ihdr, DataForm, ParmChunk;
	if (!FindChildForm(Reader, AhfrForm, TEXT("0003"), Version)) return false;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutAffector.bEnabled);

	// Unlike every other affector/boundary, HeightFractal wraps its fields in a
	// nested FORM 'DATA' (not a plain leaf chunk) containing a 'PARM' leaf —
	// confirmed against AffectorHeightFractal.cpp's parseFromIffStream(Version<'0003'>).
	if (!FindChildForm(Reader, Version, TEXT("DATA"), DataForm)) return false;
	if (!FindChildChunk(Reader, DataForm, TEXT("PARM"), ParmChunk)) return false;

	const uint8* D = Reader.GetChunkData(ParmChunk);
	OutAffector.Type = ESWGTerrainAffectorType::HeightFractal;
	OutAffector.FractalId = (int32)ReadUInt32LE(D, 0);
	OutAffector.OperationType = (int32)ReadUInt32LE(D, 4);
	OutAffector.Height = ReadFloatLE(D, 8);
	return true;
}

bool FSWGTerrainReader::ReadAffectorHeightTerrace(const FSWGIffReader& Reader, const FSWGIffChunk& AhtrForm, FSWGTerrainAffector& OutAffector)
{
	FSWGIffChunk Version, Ihdr, DataChunk;
	if (!FindChildForm(Reader, AhtrForm, TEXT("0004"), Version)) return false;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutAffector.bEnabled);

	if (!FindChildChunk(Reader, Version, TEXT("DATA"), DataChunk)) return false;

	const uint8* D = Reader.GetChunkData(DataChunk);
	OutAffector.Type = ESWGTerrainAffectorType::HeightTerrace;
	OutAffector.FlatRatio = ReadFloatLE(D, 0);
	OutAffector.Height = ReadFloatLE(D, 4);
	return true;
}

bool FSWGTerrainReader::ReadAffector(const FSWGIffReader& Reader, const FSWGIffChunk& AffectorForm, FSWGTerrainAffector& OutAffector)
{
	if (AffectorForm.FormType == TEXT("AHCN")) return ReadAffectorHeightConstant(Reader, AffectorForm, OutAffector);
	if (AffectorForm.FormType == TEXT("AHFR")) return ReadAffectorHeightFractal(Reader, AffectorForm, OutAffector);
	if (AffectorForm.FormType == TEXT("AHTR")) return ReadAffectorHeightTerrace(Reader, AffectorForm, OutAffector);
	return false;
}

bool FSWGTerrainReader::ReadLayer(const FSWGIffReader& Reader, const FSWGIffChunk& LayrForm, FSWGTerrainLayer& OutLayer)
{
	FSWGIffChunk Form0003;
	if (!FindChildForm(Reader, LayrForm, TEXT("0003"), Form0003)) return false;

	for (const FSWGIffChunk& Child : Reader.ReadChildren(Form0003))
	{
		if (Child.IsForm() && Child.FormType == TEXT("IHDR"))
		{
			ReadInformationHeader(Reader, Child, OutLayer.Name, OutLayer.bEnabled);
		}
		else if (!Child.IsForm() && Child.Tag == TEXT("ADTA"))
		{
			const uint8* D = Reader.GetChunkData(Child);
			OutLayer.bInvertBoundaries = ReadUInt32LE(D, 0) != 0;
			OutLayer.bInvertFilters = ReadUInt32LE(D, 4) != 0;
		}
		else if (Child.IsForm() && (Child.FormType == TEXT("BCIR") || Child.FormType == TEXT("BREC") || Child.FormType == TEXT("BPOL")))
		{
			FSWGTerrainBoundary Boundary;
			if (ReadBoundary(Reader, Child, Boundary))
			{
				OutLayer.Boundaries.Add(MoveTemp(Boundary));
			}
		}
		else if (Child.IsForm() && (Child.FormType == TEXT("AHCN") || Child.FormType == TEXT("AHFR") || Child.FormType == TEXT("AHTR")))
		{
			FSWGTerrainAffector Affector;
			if (ReadAffector(Reader, Child, Affector))
			{
				OutLayer.Affectors.Add(MoveTemp(Affector));
			}
		}
		else if (Child.IsForm() && Child.FormType == TEXT("LAYR"))
		{
			FSWGTerrainLayer ChildLayer;
			if (ReadLayer(Reader, Child, ChildLayer))
			{
				OutLayer.Children.Add(MoveTemp(ChildLayer));
			}
		}
		// Everything else (other affector/boundary/filter tags — ACxx/AFxx/AExx/ARxx/ASxx,
		// BALL/BPLN/BSPL, Fxxx) isn't modeled yet; skipped, matching Core3's own
		// graceful fallback for unrecognized types (Layer::parseAffector/parseBoundary
		// return nullptr and the caller just moves on).
	}

	return true;
}

bool FSWGTerrainReader::ReadTerrain(const FSWGIffReader& Reader, FSWGTerrainData& OutData)
{
	if (!Reader.IsValid()) return false;

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm() || TopLevel[0].FormType != TEXT("PTAT"))
		return false;

	// PTAT wraps a single version-tagged form (seen as "0013" on a small test file,
	// "0014" on real planet data like tatooine.trn) — take whichever one is there
	// rather than hardcoding a version, since nothing below this depends on which.
	const TArray<FSWGIffChunk> PtatChildren = FindChildForms(Reader, TopLevel[0]);
	if (PtatChildren.Num() == 0) return false;
	const FSWGIffChunk& PtatVersionForm = PtatChildren[0];

	FSWGIffChunk TgenForm, TgenForm0000;
	if (!FindChildForm(Reader, PtatVersionForm, TEXT("TGEN"), TgenForm)) return false;
	if (!FindChildForm(Reader, TgenForm, TEXT("0000"), TgenForm0000)) return false;

	// SGRP/FGRP/RGRP/EGRP (shader/flora/radial/environment groups) are skipped
	// structurally — not needed until shader/flora rendering is tackled (deferred).
	// The first MGRP (map group) IS parsed, since AffectorHeightFractal needs it; a
	// second MGRP occurrence (Core3's "bitmap group") is skipped. Once past the
	// groups, per TerrainGenerator::parseFromIffStream the next child is either a
	// single root LAYR or a LYRS wrapper containing multiple top-level LAYR entries.
	static const TSet<FString> SkippedGroupTags = { TEXT("SGRP"), TEXT("FGRP"), TEXT("RGRP"), TEXT("EGRP") };

	bool bFoundMapGroup = false;

	for (const FSWGIffChunk& Child : Reader.ReadChildren(TgenForm0000))
	{
		if (!Child.IsForm())
			continue;

		if (SkippedGroupTags.Contains(Child.FormType))
			continue;

		if (Child.FormType == TEXT("MGRP"))
		{
			if (!bFoundMapGroup)
			{
				ReadMapGroup(Reader, Child, OutData.MapGroup);
				bFoundMapGroup = true;
			}
			// else: this is Core3's "bitmapGroup" (bitmap-affector support), not needed here.
			continue;
		}

		if (Child.FormType == TEXT("LAYR"))
		{
			FSWGTerrainLayer Layer;
			if (ReadLayer(Reader, Child, Layer))
			{
				OutData.TopLevelLayers.Add(MoveTemp(Layer));
			}
		}
		else if (Child.FormType == TEXT("LYRS"))
		{
			for (const FSWGIffChunk& LayrChild : Reader.ReadChildren(Child))
			{
				if (LayrChild.IsForm() && LayrChild.FormType == TEXT("LAYR"))
				{
					FSWGTerrainLayer Layer;
					if (ReadLayer(Reader, LayrChild, Layer))
					{
						OutData.TopLevelLayers.Add(MoveTemp(Layer));
					}
				}
			}
		}

		break;
	}

	return true;
}

bool FSWGTerrainReader::ReadMapGroup(const FSWGIffReader& Reader, const FSWGIffChunk& MgrpForm, FSWGMapGroup& OutGroup)
{
	FSWGIffChunk Version;
	if (!FindChildForm(Reader, MgrpForm, TEXT("0000"), Version)) return false;

	for (const FSWGIffChunk& MfamForm : Reader.ReadChildren(Version))
	{
		if (!MfamForm.IsForm() || MfamForm.FormType != TEXT("MFAM"))
			continue;

		FSWGIffChunk FamilyDataChunk;
		if (!FindChildChunk(Reader, MfamForm, TEXT("DATA"), FamilyDataChunk)) continue;

		const uint8* FamilyData = Reader.GetChunkData(FamilyDataChunk);
		const int32 FamilyId = (int32)ReadUInt32LE(FamilyData, 0);

		FSWGIffChunk MfrcForm, MfrcVersion, MfrcData;
		if (!FindChildForm(Reader, MfamForm, TEXT("MFRC"), MfrcForm)) continue;
		if (!FindChildForm(Reader, MfrcForm, TEXT("0001"), MfrcVersion)) continue;
		if (!FindChildChunk(Reader, MfrcVersion, TEXT("DATA"), MfrcData)) continue;

		const uint8* D = Reader.GetChunkData(MfrcData);
		FSWGMapFractal Fractal;
		int32 Offset = 0;
		Fractal.Seed = (int32)ReadUInt32LE(D, Offset); Offset += 4;
		Fractal.Bias = (int32)ReadUInt32LE(D, Offset); Offset += 4;
		Fractal.BiasValue = ReadFloatLE(D, Offset); Offset += 4;
		Fractal.GainType = (int32)ReadUInt32LE(D, Offset); Offset += 4;
		Fractal.GainValue = ReadFloatLE(D, Offset); Offset += 4;
		Fractal.Octaves = (int32)ReadUInt32LE(D, Offset); Offset += 4;
		Fractal.OctavesParam = ReadFloatLE(D, Offset); Offset += 4;
		Fractal.Amplitude = ReadFloatLE(D, Offset); Offset += 4;
		Fractal.XFrequency = ReadFloatLE(D, Offset); Offset += 4;
		Fractal.YFrequency = ReadFloatLE(D, Offset); Offset += 4;
		Fractal.XOffset = ReadFloatLE(D, Offset); Offset += 4;
		Fractal.ZOffset = ReadFloatLE(D, Offset); Offset += 4;
		Fractal.Combination = (int32)ReadUInt32LE(D, Offset); Offset += 4;

		Fractal.Initialize();

		OutGroup.Fractals.Add(FamilyId, MoveTemp(Fractal));
	}

	return true;
}
