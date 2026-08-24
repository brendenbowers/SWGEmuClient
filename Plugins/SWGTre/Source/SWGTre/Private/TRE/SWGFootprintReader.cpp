#include "TRE/SWGFootprintReader.h"

#include "TRE/SWGIFFChunkReader.h"

bool FSWGStructureFootprint::GetStructureCellBounds(FIntPoint& OutMin, FIntPoint& OutMax) const
{
	if (!IsValid())
	{
		return false;
	}

	bool bFound = false;
	FIntPoint Min(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint Max(TNumericLimits<int32>::Lowest(), TNumericLimits<int32>::Lowest());

	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Col = 0; Col < Cols; ++Col)
		{
			if (GetCell(Col, Row) != ESWGFootprintCell::Structure)
			{
				continue;
			}

			bFound = true;
			Min.X = FMath::Min(Min.X, Col);
			Min.Y = FMath::Min(Min.Y, Row);
			Max.X = FMath::Max(Max.X, Col);
			Max.Y = FMath::Max(Max.Y, Row);
		}
	}

	if (!bFound)
	{
		return false;
	}

	OutMin = Min;
	OutMax = Max;
	return true;
}

bool FSWGStructureFootprint::GetStructureLocalBounds(FBox2D& OutBounds) const
{
	FIntPoint MinCell, MaxCell;
	if (!GetStructureCellBounds(MinCell, MaxCell))
	{
		return false;
	}

	const FVector2D Min(
		((float)(MinCell.X - CenterCol) - 0.5f) * ColChunkSize,
		((float)(MinCell.Y - CenterRow) - 0.5f) * RowChunkSize);
	const FVector2D Max(
		((float)(MaxCell.X - CenterCol) + 0.5f) * ColChunkSize,
		((float)(MaxCell.Y - CenterRow) + 0.5f) * RowChunkSize);

	OutBounds = FBox2D(Min, Max);
	return true;
}

bool FSWGFootprintReader::Read(const FSWGIffReader& Reader, FSWGStructureFootprint& OutFootprint)
{
	OutFootprint = FSWGStructureFootprint();

	FSWGIffChunk FootForm, Form0000, InfoChunk, PrntChunk;
	if (!Reader.FindForm(SWG_IFF_TAG('F', 'O', 'O', 'T'), FootForm)
		|| !Reader.FindChildForm(FootForm, SWG_IFF_TAG('0', '0', '0', '0'), Form0000)
		|| !Reader.FindChildChunk(Form0000, SWG_IFF_TAG('I', 'N', 'F', 'O'), InfoChunk)
		|| !Reader.FindChildChunk(Form0000, SWG_IFF_TAG('P', 'R', 'N', 'T'), PrntChunk))
	{
		return false;
	}

	FSWGIFFChunkReader Info(InfoChunk, Reader);
	int32 Cols = 0, Rows = 0, CenterCol = 0, CenterRow = 0;
	float ColChunkSize = 0.0f, RowChunkSize = 0.0f;
	if (!Info.ReadValueLE(Cols) || !Info.ReadValueLE(Rows)
		|| !Info.ReadValueLE(CenterCol) || !Info.ReadValueLE(CenterRow)
		|| !Info.ReadValueLE(ColChunkSize) || !Info.ReadValueLE(RowChunkSize))
	{
		return false;
	}

	if (Cols <= 0 || Rows <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGFootprintReader: footprint INFO has non-positive extent (%dx%d)"), Cols, Rows);
		return false;
	}

	FSWGIFFChunkReader Prnt(PrntChunk, Reader);
	const TArray<FString> RowStrings = Prnt.ReadTerminatedStrings(Rows);
	if (RowStrings.Num() < Rows)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGFootprintReader: footprint PRNT has %d rows, INFO declared %d"), RowStrings.Num(), Rows);
		return false;
	}

	OutFootprint.Cols = Cols;
	OutFootprint.Rows = Rows;
	OutFootprint.CenterCol = CenterCol;
	OutFootprint.CenterRow = CenterRow;
	OutFootprint.ColChunkSize = ColChunkSize;
	OutFootprint.RowChunkSize = RowChunkSize;
	OutFootprint.Cells.Init(ESWGFootprintCell::Outside, Cols * Rows);

	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const FString& RowString = RowStrings[Row];
		if (RowString.Len() < Cols)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGFootprintReader: footprint PRNT row %d is %d chars, INFO declared %d columns"), Row, RowString.Len(), Cols);
			return false;
		}

		for (int32 Col = 0; Col < Cols; ++Col)
		{
			ESWGFootprintCell Cell = ESWGFootprintCell::Outside;
			switch (RowString[Col])
			{
				case TEXT('F'): Cell = ESWGFootprintCell::Structure; break;
				case TEXT('H'): Cell = ESWGFootprintCell::Reserved; break;
				case TEXT('.'): Cell = ESWGFootprintCell::Outside; break;
				default:
					// No retail footprint uses any other character (see the
					// histogram in the header) — log rather than guess, so a
					// modded/custom .sfp doesn't silently flatten the wrong cells.
					UE_LOG(LogTemp, Warning, TEXT("FSWGFootprintReader: unrecognised footprint cell '%c' at (%d,%d) — treating as outside"), RowString[Col], Col, Row);
					break;
			}

			OutFootprint.Cells[Row * Cols + Col] = Cell;
		}
	}

	return true;
}

bool FSWGFootprintReader::BuildFlattenLayer(const FSWGStructureFootprint& Footprint, const FSWGFootprintFlattenParams& Params, FSWGTerrainLayer& OutLayer)
{
	FBox2D LocalBounds;
	if (!Footprint.GetStructureLocalBounds(LocalBounds))
	{
		return false;
	}

	const float SinYaw = FMath::Sin(Params.YawRadians);
	const float CosYaw = FMath::Cos(Params.YawRadians);

	auto ToWorld = [&](const FVector2D& Local)
	{
		return FVector2D(
			Params.WorldCenter.X + Local.X * CosYaw - Local.Y * SinYaw,
			Params.WorldCenter.Y + Local.X * SinYaw + Local.Y * CosYaw);
	};

	// See FSWGFootprintFlattenParams::FeatheringDistance — this is a polygon, so
	// FeatheringAmount is an absolute distance in metres here, not a fraction.
	float FeatheringDistance = Params.FeatheringDistance;
	if (FeatheringDistance <= 0.0f)
	{
		FeatheringDistance = FMath::Min(Footprint.ColChunkSize, Footprint.RowChunkSize) * 0.5f;
	}

	// Keep the ramp from ballooning the flattened area on footprints with very
	// large cells (retail goes up to 32 m).
	const FVector2D PadSize = LocalBounds.GetSize();
	FeatheringDistance = FMath::Clamp(FeatheringDistance, 0.0f, (float)FMath::Min(PadSize.X, PadSize.Y) * 0.5f);

	// EvaluateBoundaryPolygon feathers INWARD from the boundary: at the edge the
	// affector has no effect, reaching full strength only FeatheringDistance
	// inside. Using the footprint outline directly therefore leaves its outer
	// ring only partially flattened — and the structure's walls sit exactly on
	// that ring, so terrain still surfaces inside rooms at the corners.
	//
	// Grow the polygon by the feather distance so the ramp lives entirely
	// outside the footprint and every cell the footprint actually covers gets
	// the full flatten. The growth stays within the 'H' clearance ring the
	// footprint already reserves around the structure for exactly this kind of
	// margin, so it does not encroach on anywhere another structure could be.
	const FBox2D PaddedBounds = LocalBounds.ExpandBy(FeatheringDistance);

	FSWGTerrainBoundary Boundary;
	Boundary.Type = ESWGTerrainBoundaryType::Polygon;
	Boundary.bEnabled = true;
	Boundary.FeatheringType = Params.FeatheringType;
	Boundary.FeatheringAmount = FeatheringDistance;
	Boundary.Vertices = {
		ToWorld(FVector2D(PaddedBounds.Min.X, PaddedBounds.Min.Y)),
		ToWorld(FVector2D(PaddedBounds.Max.X, PaddedBounds.Min.Y)),
		ToWorld(FVector2D(PaddedBounds.Max.X, PaddedBounds.Max.Y)),
		ToWorld(FVector2D(PaddedBounds.Min.X, PaddedBounds.Max.Y)),
	};

	FSWGTerrainAffector Affector;
	Affector.Type = ESWGTerrainAffectorType::HeightConstant;
	Affector.bEnabled = true;
	// OperationType 0 is the default "lerp" branch of AffectorHeightConstant::process
	// — it blends between existing terrain and Height by the boundary's transform
	// value, so the feathered rim ramps in instead of stepping. Any of the
	// add/subtract/scale ops would offset the terrain rather than replace it.
	Affector.OperationType = 0;
	Affector.Height = Params.BaseHeight;

	OutLayer = FSWGTerrainLayer();
	OutLayer.Name = TEXT("StructureFootprintFlatten");
	OutLayer.bEnabled = true;
	OutLayer.Boundaries.Add(MoveTemp(Boundary));
	OutLayer.Affectors.Add(MoveTemp(Affector));

	return true;
}
