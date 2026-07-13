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

bool FSWGTerrainReader::ReadBoundaryPolyline(const FSWGIffReader& Reader, const FSWGIffChunk& BplnForm, FSWGTerrainBoundary& OutBoundary)
{
	FSWGIffChunk Version, Ihdr, DataChunk;
	if (!FindChildForm(Reader, BplnForm, TEXT("0001"), Version)) return false;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutBoundary.bEnabled);

	if (!FindChildChunk(Reader, Version, TEXT("DATA"), DataChunk)) return false;

	const uint8* D = Reader.GetChunkData(DataChunk);
	OutBoundary.Type = ESWGTerrainBoundaryType::Polyline;

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
	OutBoundary.LineWidth = ReadFloatLE(D, Offset + 8);

	return true;
}

bool FSWGTerrainReader::ReadBoundary(const FSWGIffReader& Reader, const FSWGIffChunk& BoundaryForm, FSWGTerrainBoundary& OutBoundary)
{
	if (BoundaryForm.FormType == TEXT("BCIR")) return ReadBoundaryCircle(Reader, BoundaryForm, OutBoundary);
	if (BoundaryForm.FormType == TEXT("BREC")) return ReadBoundaryRectangle(Reader, BoundaryForm, OutBoundary);
	if (BoundaryForm.FormType == TEXT("BPOL")) return ReadBoundaryPolygon(Reader, BoundaryForm, OutBoundary);
	if (BoundaryForm.FormType == TEXT("BPLN")) return ReadBoundaryPolyline(Reader, BoundaryForm, OutBoundary);
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

void FSWGTerrainReader::BuildRoadwayHeights(FSWGTerrainRoadSegment& Segment)
{
	// Confirmed port of Segment::createRoadwayHeights.
	if (Segment.bFlatRoad || Segment.Positions.Num() < 3)
	{
		return;
	}

	const TArray<FSWGTerrainRoadPoint> OldPositions = Segment.Positions;
	Segment.Positions.Reset();
	Segment.Positions.Add(OldPositions[0]);

	const int32 TotalPositions = OldPositions.Num();
	const FSWGTerrainRoadPoint EndPoint = OldPositions[TotalPositions - 1];

	for (int32 i = 1; i < TotalPositions - 1; ++i)
	{
		const float ZAverage = (OldPositions[i - 1].Z + OldPositions[i].Z + OldPositions[i + 1].Z) / 3.0f;

		FSWGTerrainRoadPoint NewPoint;
		NewPoint.X = OldPositions[i].X;
		NewPoint.Y = OldPositions[i].Y;
		NewPoint.Z = ZAverage;
		Segment.Positions.Add(NewPoint);
	}

	Segment.Positions.Add(EndPoint);
}

void FSWGTerrainReader::GenerateRoadRectangles(FSWGTerrainAffector& OutAffector, const TArray<FVector2D>& MidPositions, const FVector2D& EndPoint)
{
	// Confirmed port of AffectorRoad::generateRectangles/addNewRectangle.
	auto AddRectangle = [&OutAffector](float X1, float Y1, float X2, float Y2)
	{
		const float WidthHalf = OutAffector.RoadWidth / 2.0f;

		const float DeltaX = X1 - X2;
		const float DeltaY = Y1 - Y2;
		float DirectionAngle = FMath::Atan2(DeltaY, DeltaX);

		DirectionAngle = (PI / 2.0f) - DirectionAngle;
		if (DirectionAngle < 0.0f)
		{
			const float A = PI + DirectionAngle;
			DirectionAngle = PI + A;
		}

		const float XLowerLeft = X2 + (-WidthHalf * FMath::Cos(DirectionAngle));
		const float YLowerLeft = Y2 + (WidthHalf * FMath::Sin(DirectionAngle));
		const float XUpperRight = X1 + (WidthHalf * FMath::Cos(DirectionAngle));
		const float YUpperRight = Y1 + (-WidthHalf * FMath::Sin(DirectionAngle));

		FSWGTerrainRoadRectangle Rect;
		Rect.CenterX = (XLowerLeft + XUpperRight) * 0.5f;
		Rect.CenterY = (YLowerLeft + YUpperRight) * 0.5f;
		Rect.Width = OutAffector.RoadWidth;
		Rect.Height = FMath::Sqrt(FMath::Square(XUpperRight - XLowerLeft) + FMath::Square(YUpperRight - YLowerLeft));
		Rect.Direction = DirectionAngle;
		Rect.RoadStartX = X1;
		Rect.RoadStartY = Y1;

		OutAffector.RoadRectangles.Add(Rect);
	};

	if (MidPositions.Num() == 0)
	{
		AddRectangle(OutAffector.RoadStartPoint.X, OutAffector.RoadStartPoint.Y, EndPoint.X, EndPoint.Y);
		return;
	}

	FVector2D Point1 = OutAffector.RoadStartPoint;
	for (const FVector2D& Position : MidPositions)
	{
		AddRectangle(Point1.X, Point1.Y, Position.X, Position.Y);
		Point1 = Position;
	}

	AddRectangle(Point1.X, Point1.Y, EndPoint.X, EndPoint.Y);
}

bool FSWGTerrainReader::ReadAffectorRoad(const FSWGIffReader& Reader, const FSWGIffChunk& AroaForm, FSWGTerrainAffector& OutAffector)
{
	FSWGIffChunk Version, Ihdr;
	if (!FindChildForm(Reader, AroaForm, TEXT("0005"), Version)) return false;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutAffector.bEnabled);

	OutAffector.Type = ESWGTerrainAffectorType::Road;

	// Outer FORM DATA holds a child FORM (either 'ROAD' — shader/visual only,
	// out of scope — or 'HDTA', the height profile we need), then a sibling
	// leaf CHUNK also tagged "DATA" with the path coordinates/width/etc.
	FSWGIffChunk OuterDataForm;
	if (!FindChildForm(Reader, Version, TEXT("DATA"), OuterDataForm)) return false;

	FSWGIffChunk InnerDataChunk;
	bool bFoundInnerDataChunk = false;

	for (const FSWGIffChunk& Child : Reader.ReadChildren(OuterDataForm))
	{
		if (Child.IsForm() && Child.FormType == TEXT("HDTA"))
		{
			OutAffector.bRoadIsHeightType = true;

			// FORM HDTA > FORM 0001 > multiple 'SGMT' leaf chunks, each a
			// packed list of (x,z,y) float triples — confirmed port of
			// HeightData::parseFromIffStream/Segment::readObject.
			FSWGIffChunk HdtaVersion;
			if (FindChildForm(Reader, Child, TEXT("0001"), HdtaVersion))
			{
				for (const FSWGIffChunk& SegChild : Reader.ReadChildren(HdtaVersion))
				{
					if (SegChild.Tag != TEXT("SGMT")) continue;

					const uint8* D = Reader.GetChunkData(SegChild);
					const int32 Size = Reader.GetChunkSize(SegChild);
					const int32 Count = Size / 12;

					FSWGTerrainRoadSegment Segment;
					Segment.Positions.Reserve(Count);

					float LastZ = 0.0f;
					for (int32 i = 0; i < Count; ++i)
					{
						FSWGTerrainRoadPoint Point;
						Point.X = ReadFloatLE(D, i * 12 + 0);
						Point.Z = ReadFloatLE(D, i * 12 + 4);
						Point.Y = ReadFloatLE(D, i * 12 + 8);

						Segment.bFlatRoad = (Point.Z == LastZ);
						LastZ = Point.Z;
						Segment.Positions.Add(Point);
					}

					BuildRoadwayHeights(Segment);
					OutAffector.RoadSegments.Add(MoveTemp(Segment));
				}
			}
		}
		// FORM 'ROAD' (shader/visual road type) is deliberately not parsed —
		// AffectorRoad::process itself only ever touches height for the
		// 'HDTA' type, so there's nothing for GetHeight to gain from it.
		else if (!Child.IsForm() && Child.Tag == TEXT("DATA"))
		{
			InnerDataChunk = Child;
			bFoundInnerDataChunk = true;
		}
	}

	if (!bFoundInnerDataChunk) return false;

	const uint8* D = Reader.GetChunkData(InnerDataChunk);
	int32 Offset = 0;

	const int32 CoordinateCount = (int32)ReadUInt32LE(D, Offset);
	Offset += 4;

	TArray<FVector2D> MidPositions;
	FVector2D EndPoint = FVector2D::ZeroVector;

	for (int32 i = 0; i < CoordinateCount; ++i)
	{
		const float X = ReadFloatLE(D, Offset);
		const float Y = ReadFloatLE(D, Offset + 4);
		Offset += 8;

		if (i == 0)
		{
			OutAffector.RoadStartPoint = FVector2D(X, Y);
		}
		else if (i == CoordinateCount - 1)
		{
			EndPoint = FVector2D(X, Y);
		}
		else
		{
			MidPositions.Add(FVector2D(X, Y));
		}
	}

	const float RawWidth = ReadFloatLE(D, Offset);
	OutAffector.RoadWidth = RawWidth * 0.7f; // Core3's own fudge factor.
	// familyID/featheringType/featheringAmount/featheringShader/featheringShaderDistance
	// follow — not needed for height evaluation, so not read.

	GenerateRoadRectangles(OutAffector, MidPositions, EndPoint);

	return true;
}

bool FSWGTerrainReader::ReadAffectorShaderConstant(const FSWGIffReader& Reader, const FSWGIffChunk& AscnForm, FSWGTerrainAffector& OutAffector)
{
	// Confirmed port of AffectorShaderConstant::parseFromIffStream(Version<'0001'>).
	FSWGIffChunk Version, Ihdr, DataChunk;
	if (!FindChildForm(Reader, AscnForm, TEXT("0001"), Version)) return false;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutAffector.bEnabled);

	if (!FindChildChunk(Reader, Version, TEXT("DATA"), DataChunk)) return false;

	const uint8* D = Reader.GetChunkData(DataChunk);
	OutAffector.Type = ESWGTerrainAffectorType::ShaderConstant;
	OutAffector.ShaderFamilyId = (int32)ReadUInt32LE(D, 0);
	OutAffector.ShaderFeatheringType = (int32)ReadUInt32LE(D, 4);
	OutAffector.ShaderFeatheringAmount = ReadFloatLE(D, 8);
	return true;
}

bool FSWGTerrainReader::ReadAffectorShaderReplace(const FSWGIffReader& Reader, const FSWGIffChunk& AsrpForm, FSWGTerrainAffector& OutAffector)
{
	// Confirmed port of AffectorShaderReplace::parseFromIffStream(Version<'0001'>).
	FSWGIffChunk Version, Ihdr, DataChunk;
	if (!FindChildForm(Reader, AsrpForm, TEXT("0001"), Version)) return false;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutAffector.bEnabled);

	if (!FindChildChunk(Reader, Version, TEXT("DATA"), DataChunk)) return false;

	const uint8* D = Reader.GetChunkData(DataChunk);
	OutAffector.Type = ESWGTerrainAffectorType::ShaderReplace;
	OutAffector.ShaderOldFamilyId = (int32)ReadUInt32LE(D, 0);
	OutAffector.ShaderNewFamilyId = (int32)ReadUInt32LE(D, 4);
	OutAffector.ShaderFeatheringType = (int32)ReadUInt32LE(D, 8);
	OutAffector.ShaderFeatheringAmount = ReadFloatLE(D, 12);
	return true;
}

bool FSWGTerrainReader::ReadAffector(const FSWGIffReader& Reader, const FSWGIffChunk& AffectorForm, FSWGTerrainAffector& OutAffector)
{
	if (AffectorForm.FormType == TEXT("AHCN")) return ReadAffectorHeightConstant(Reader, AffectorForm, OutAffector);
	if (AffectorForm.FormType == TEXT("AHFR")) return ReadAffectorHeightFractal(Reader, AffectorForm, OutAffector);
	if (AffectorForm.FormType == TEXT("AHTR")) return ReadAffectorHeightTerrace(Reader, AffectorForm, OutAffector);
	if (AffectorForm.FormType == TEXT("AROA")) return ReadAffectorRoad(Reader, AffectorForm, OutAffector);
	if (AffectorForm.FormType == TEXT("ASCN")) return ReadAffectorShaderConstant(Reader, AffectorForm, OutAffector);
	if (AffectorForm.FormType == TEXT("ASRP")) return ReadAffectorShaderReplace(Reader, AffectorForm, OutAffector);
	return false;
}

bool FSWGTerrainReader::ReadFilterHeight(const FSWGIffReader& Reader, const FSWGIffChunk& FhgtForm, FSWGTerrainFilter& OutFilter)
{
	FSWGIffChunk Version, Ihdr, DataChunk;
	if (!FindChildForm(Reader, FhgtForm, TEXT("0002"), Version)) return false;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutFilter.bEnabled);

	if (!FindChildChunk(Reader, Version, TEXT("DATA"), DataChunk)) return false;

	const uint8* D = Reader.GetChunkData(DataChunk);
	OutFilter.Type = ESWGTerrainFilterType::Height;
	OutFilter.MinHeight = ReadFloatLE(D, 0);
	OutFilter.MaxHeight = ReadFloatLE(D, 4);
	OutFilter.FeatheringType = (int32)ReadUInt32LE(D, 8);
	OutFilter.FeatheringAmount = ReadFloatLE(D, 12);
	return true;
}

bool FSWGTerrainReader::ReadFilterFractal(const FSWGIffReader& Reader, const FSWGIffChunk& FfraForm, FSWGTerrainFilter& OutFilter)
{
	// Confirmed port of FilterFractal::parseFromIffStream(Version<'0005'>) —
	// version form 0005 > IHDR, then (unlike FilterHeight) a FORM DATA
	// containing a CHUNK PARM, not a bare DATA chunk.
	FSWGIffChunk Version, Ihdr, DataForm, ParmChunk;
	if (!FindChildForm(Reader, FfraForm, TEXT("0005"), Version)) return false;
	if (!FindChildForm(Reader, Version, TEXT("IHDR"), Ihdr)) return false;

	FString UnusedName;
	ReadInformationHeader(Reader, Ihdr, UnusedName, OutFilter.bEnabled);

	if (!FindChildForm(Reader, Version, TEXT("DATA"), DataForm)) return false;
	if (!FindChildChunk(Reader, DataForm, TEXT("PARM"), ParmChunk)) return false;

	const uint8* D = Reader.GetChunkData(ParmChunk);
	OutFilter.Type = ESWGTerrainFilterType::Fractal;
	OutFilter.FractalId = (int32)ReadUInt32LE(D, 0);
	OutFilter.FeatheringType = (int32)ReadUInt32LE(D, 4);
	OutFilter.FeatheringAmount = ReadFloatLE(D, 8);
	OutFilter.Min = ReadFloatLE(D, 12);
	OutFilter.Max = ReadFloatLE(D, 16);
	OutFilter.Scale = ReadFloatLE(D, 20);

	UE_LOG(LogTemp, Warning, TEXT("FSWGTerrainReader: parsed FilterFractal fractalId=%d featheringType=%d featheringAmount=%.4f min=%.4f max=%.4f scale=%.4f"),
		OutFilter.FractalId, OutFilter.FeatheringType, OutFilter.FeatheringAmount, OutFilter.Min, OutFilter.Max, OutFilter.Scale);

	return true;
}

bool FSWGTerrainReader::ReadFilter(const FSWGIffReader& Reader, const FSWGIffChunk& FilterForm, FSWGTerrainFilter& OutFilter)
{
	if (FilterForm.FormType == TEXT("FHGT")) return ReadFilterHeight(Reader, FilterForm, OutFilter);
	if (FilterForm.FormType == TEXT("FFRA")) return ReadFilterFractal(Reader, FilterForm, OutFilter);
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
		else if (Child.IsForm() && (Child.FormType == TEXT("BCIR") || Child.FormType == TEXT("BREC") || Child.FormType == TEXT("BPOL") || Child.FormType == TEXT("BPLN")))
		{
			FSWGTerrainBoundary Boundary;
			if (ReadBoundary(Reader, Child, Boundary))
			{
				OutLayer.Boundaries.Add(MoveTemp(Boundary));
			}
		}
		else if (Child.IsForm() && (Child.FormType == TEXT("FHGT") || Child.FormType == TEXT("FFRA")))
		{
			FSWGTerrainFilter Filter;
			if (ReadFilter(Reader, Child, Filter))
			{
				OutLayer.Filters.Add(MoveTemp(Filter));
			}
		}
		else if (Child.IsForm() && (Child.FormType == TEXT("AHCN") || Child.FormType == TEXT("AHFR") || Child.FormType == TEXT("AHTR") || Child.FormType == TEXT("AROA") || Child.FormType == TEXT("ASCN") || Child.FormType == TEXT("ASRP")))
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
		else if (Child.IsForm() && Child.FormType != TEXT("0003") && Child.FormType != TEXT("ADTA"))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGTerrainReader: unrecognized layer child form '%s' (layer '%s') — skipped"),
				*Child.FormType, *OutLayer.Name);

			if (Child.FormType.StartsWith(TEXT("B")) || Child.FormType.StartsWith(TEXT("F")))
			{
				OutLayer.SkippedBoundaryOrFilterTags.Add(Child.FormType);
			}
		}
	}

	return true;
}

bool FSWGTerrainReader::ReadShadersGroup(const FSWGIffReader& Reader, const FSWGIffChunk& SgrpForm, TArray<FSWGShaderFamily>& OutFamilies)
{
	// SGRP's own version form directly holds a flat list of SFAM chunks (not
	// forms): [familyId:int32][familyName][fileName][r,g,b:uint8][var7:float]
	// [weight:float][numLayers:int32] then that many {name, weight} layer
	// entries. Only the per-layer names are needed for texture resolution.
	const TArray<FSWGIffChunk> VersionForms = FindChildForms(Reader, SgrpForm);
	if (VersionForms.Num() == 0) return false;

	for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForms[0]))
	{
		if (Child.IsForm() || Child.Tag != TEXT("SFAM"))
		{
			continue;
		}

		const uint8* D = Reader.GetChunkData(Child);
		const int32 Size = Reader.GetChunkSize(Child);
		int32 Offset = 0;

		if (Offset + 4 > Size) continue;
		const int32 FamilyId = (int32)ReadUInt32LE(D, Offset);
		Offset += 4;

		auto ReadNullTerminated = [D, Size](int32& InOutOffset) -> FString
		{
			const int32 Start = InOutOffset;
			while (InOutOffset < Size && D[InOutOffset] != 0) { ++InOutOffset; }
			FString Result = FString::ConstructFromPtrSize((const ANSICHAR*)(D + Start), InOutOffset - Start);
			++InOutOffset; // skip null terminator
			return Result;
		};

		FSWGShaderFamily Family;
		Family.FamilyId = FamilyId;
		Family.Name = ReadNullTerminated(Offset);
		ReadNullTerminated(Offset); // fileName — gameplay metadata (cover/surfaceType), not a texture reference; see FSWGShaderFamily's comment.

		Offset += 3; // color (3 bytes)
		Offset += 8; // var7 + weight (floats)

		if (Offset + 4 > Size) continue;
		const int32 NumLayers = (int32)ReadUInt32LE(D, Offset);
		Offset += 4;

		for (int32 L = 0; L < NumLayers && Offset < Size; ++L)
		{
			Family.LayerNames.Add(ReadNullTerminated(Offset));
			Offset += 4; // layer weight (float) — not needed, we only use layer[0] as the primary texture.
		}

		OutFamilies.Add(MoveTemp(Family));
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

	// FGRP/RGRP/EGRP (flora/radial/environment groups) are skipped structurally —
	// not needed until flora rendering is tackled (deferred). SGRP (shader group,
	// the paintable-texture-family table) IS parsed now — see FSWGShaderFamily.
	// The first MGRP (map group) IS parsed, since AffectorHeightFractal needs it; a
	// second MGRP occurrence (Core3's "bitmap group") is skipped. Once past the
	// groups, per TerrainGenerator::parseFromIffStream the next child is either a
	// single root LAYR or a LYRS wrapper containing multiple top-level LAYR entries.
	static const TSet<FString> SkippedGroupTags = { TEXT("FGRP"), TEXT("RGRP"), TEXT("EGRP") };

	bool bFoundMapGroup = false;

	for (const FSWGIffChunk& Child : Reader.ReadChildren(TgenForm0000))
	{
		if (!Child.IsForm())
			continue;

		if (SkippedGroupTags.Contains(Child.FormType))
			continue;

		if (Child.FormType == TEXT("SGRP"))
		{
			ReadShadersGroup(Reader, Child, OutData.ShaderFamilies);
			continue;
		}

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

	// Diagnostic: finds every layer with a real height affector that's also
	// missing a boundary/filter constraint we don't parse — those are the
	// layers most likely to affect a much larger area than the retail client shows.
	TFunction<void(const FSWGTerrainLayer&)> DumpSuspectLayers = [&DumpSuspectLayers](const FSWGTerrainLayer& Layer)
	{
		const bool bHasHeightAffector = Layer.Affectors.ContainsByPredicate([](const FSWGTerrainAffector& A)
		{
			return A.Type == ESWGTerrainAffectorType::HeightConstant
				|| A.Type == ESWGTerrainAffectorType::HeightFractal
				|| A.Type == ESWGTerrainAffectorType::HeightTerrace
				|| (A.Type == ESWGTerrainAffectorType::Road && A.bRoadIsHeightType);
		});

		if (bHasHeightAffector && Layer.SkippedBoundaryOrFilterTags.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGTerrainReader: SUSPECT layer '%s' has %d height affector(s), %d recognized boundary(s), but skipped tags: %s"),
				*Layer.Name, Layer.Affectors.Num(), Layer.Boundaries.Num(), *FString::Join(Layer.SkippedBoundaryOrFilterTags, TEXT(",")));
		}

		for (const FSWGTerrainLayer& Child : Layer.Children)
		{
			DumpSuspectLayers(Child);
		}
	};

	for (const FSWGTerrainLayer& Layer : OutData.TopLevelLayers)
	{
		DumpSuspectLayers(Layer);
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
