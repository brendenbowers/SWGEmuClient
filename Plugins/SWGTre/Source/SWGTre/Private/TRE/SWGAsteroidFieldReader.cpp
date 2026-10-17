#include "TRE/SWGAsteroidFieldReader.h"
#include "TRE/SWGDataTableReader.h"

namespace
{
	bool ParseNumber(const FSWGDataTableData& Table, int32 Row, const TCHAR* Column, float& Out)
	{
		return LexTryParseString(Out, *Table.GetCell(Row, Column)) && FMath::IsFinite(Out);
	}
}

bool FSWGAsteroidFieldReader::Read(const FSWGDataTableData& Table, TArray<FSWGAsteroidFieldData>& Out)
{
	if (Table.Rows.Num() > 4096) return false;
	const TCHAR* Required[] = {TEXT("Name"),TEXT("Type"),TEXT("SplineControlPoints"),TEXT("CenterLocationX"),TEXT("CenterLocationY"),TEXT("CenterLocationZ"),
		TEXT("Radius"),TEXT("RadialStyleTable"),TEXT("NumRadialAsteroids"),TEXT("MaxViewableDistance"),TEXT("ViewAllDistance")};
	for (const TCHAR* Column : Required) if (Table.GetColumnIndex(Column) == INDEX_NONE) return false;
	TArray<FSWGAsteroidFieldData> Parsed;
	for (int32 Row = 0; Row < Table.Rows.Num(); ++Row)
	{
		FSWGAsteroidFieldData Data;
		Data.Name = Table.GetCell(Row, TEXT("Name"));
		Data.bSpline = Table.GetCell(Row, TEXT("Type")) == TEXT("2");
		float CenterX, CenterY, CenterZ, MaxViewable, ViewAll, Count;
		if (!ParseNumber(Table, Row, TEXT("CenterLocationX"), CenterX) || !ParseNumber(Table, Row, TEXT("CenterLocationY"), CenterY)
			|| !ParseNumber(Table, Row, TEXT("CenterLocationZ"), CenterZ) || !ParseNumber(Table, Row, TEXT("Radius"), Data.Radius)
			|| !ParseNumber(Table, Row, TEXT("MaxViewableDistance"), MaxViewable) || !ParseNumber(Table, Row, TEXT("ViewAllDistance"), ViewAll)
			|| !ParseNumber(Table, Row, TEXT("NumRadialAsteroids"), Count))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGAsteroidFieldReader: row %d (%s) has a numeric cell that does not parse (x '%s' y '%s' z '%s' radius '%s' max '%s' all '%s' count '%s')"), Row, *Data.Name,
				*Table.GetCell(Row, TEXT("CenterLocationX")), *Table.GetCell(Row, TEXT("CenterLocationY")), *Table.GetCell(Row, TEXT("CenterLocationZ")), *Table.GetCell(Row, TEXT("Radius")),
				*Table.GetCell(Row, TEXT("MaxViewableDistance")), *Table.GetCell(Row, TEXT("ViewAllDistance")), *Table.GetCell(Row, TEXT("NumRadialAsteroids")));
			return false;
		}
		Data.Center = FVector(CenterX, CenterY, CenterZ); Data.MaxViewableDistance = MaxViewable; Data.ViewAllDistance = ViewAll;
		Data.NumRadialAsteroids = int32(Count);
		if (Count < 0 || Count > 4096)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGAsteroidFieldReader: row %d (%s) has radial asteroid count %g out of range"), Row, *Data.Name, Count);
			return false;
		}
		if (Table.GetColumnIndex(TEXT("ScaleMin")) != INDEX_NONE && Table.GetColumnIndex(TEXT("ScaleMax")) != INDEX_NONE)
		{
			if (!ParseNumber(Table, Row, TEXT("ScaleMin"), Data.ScaleMin) || !ParseNumber(Table, Row, TEXT("ScaleMax"), Data.ScaleMax)
				|| Data.ScaleMin <= 0 || Data.ScaleMax < Data.ScaleMin || Data.ScaleMax > 100.f)
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGAsteroidFieldReader: row %d (%s) has a scale range '%s'..'%s' that is not 0 < min <= max <= 100"), Row, *Data.Name,
					*Table.GetCell(Row, TEXT("ScaleMin")), *Table.GetCell(Row, TEXT("ScaleMax")));
				return false;
			}
		}
		Data.RadialStyleTable = Table.GetCell(Row, TEXT("RadialStyleTable"));
		if (Data.Radius < 0 || Data.Radius > 1.e6f || Data.Center.GetAbsMax() > 1.e6)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGAsteroidFieldReader: row %d (%s) has radius %g at %s out of range"), Row, *Data.Name, Data.Radius, *Data.Center.ToString());
			return false;
		}
		// Control points are "x,y,z:x,y,z:...".
		TArray<FString> Points;
		Table.GetCell(Row, TEXT("SplineControlPoints")).ParseIntoArray(Points, TEXT(":"));
		for (const FString& Point : Points)
		{
			TArray<FString> Components;
			Point.ParseIntoArray(Components, TEXT(","));
			double X, Y, Z;
			if (Components.Num() != 3 || !LexTryParseString(X, *Components[0]) || !LexTryParseString(Y, *Components[1]) || !LexTryParseString(Z, *Components[2]))
			{
				// Retail typo (space_endor death_watch_1 has "3436,-5292"): skip the point, the spline joins its neighbours.
				UE_LOG(LogTemp, Warning, TEXT("FSWGAsteroidFieldReader: row %d (%s) skips a bad control point '%s'"), Row, *Data.Name, *Point);
				continue;
			}
			Data.ControlPoints.Add(FVector(X, Y, Z));
		}
		if (Data.bSpline && Data.ControlPoints.IsEmpty()) Data.bSpline = false;
		Parsed.Add(MoveTemp(Data));
	}
	Out = MoveTemp(Parsed);
	return true;
}

bool FSWGAsteroidFieldReader::ReadRadialStyle(const FSWGDataTableData& Table, TArray<FSWGAsteroidRadialStyleRow>& Out)
{
	if (Table.Rows.IsEmpty() || Table.Rows.Num() > 256) return false;
	const TCHAR* Required[] = {TEXT("Appearance"),TEXT("Likelihood"),TEXT("ScaleMin"),TEXT("ScaleMax"),TEXT("SpawnRange"),TEXT("DeleteRange")};
	for (const TCHAR* Column : Required) if (Table.GetColumnIndex(Column) == INDEX_NONE) return false;
	TArray<FSWGAsteroidRadialStyleRow> Parsed;
	for (int32 Row = 0; Row < Table.Rows.Num(); ++Row)
	{
		FSWGAsteroidRadialStyleRow Style;
		Style.Appearance = Table.GetCell(Row, TEXT("Appearance"));
		Style.HitEffect = Table.GetColumnIndex(TEXT("HitEffect")) != INDEX_NONE ? Table.GetCell(Row, TEXT("HitEffect")) : FString();
		if (Style.Appearance.IsEmpty() || !ParseNumber(Table, Row, TEXT("Likelihood"), Style.Likelihood) || !ParseNumber(Table, Row, TEXT("ScaleMin"), Style.ScaleMin)
			|| !ParseNumber(Table, Row, TEXT("ScaleMax"), Style.ScaleMax) || !ParseNumber(Table, Row, TEXT("SpawnRange"), Style.SpawnRange) || !ParseNumber(Table, Row, TEXT("DeleteRange"), Style.DeleteRange)
			|| Style.Likelihood < 0 || Style.ScaleMin <= 0 || Style.ScaleMax < Style.ScaleMin || Style.ScaleMax > 100.f
			|| Style.SpawnRange <= 0 || Style.DeleteRange < Style.SpawnRange || Style.DeleteRange > 1.e6f)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGAsteroidFieldReader: radial style row %d ('%s') is invalid (likelihood '%s' scale '%s'..'%s' spawn '%s' delete '%s')"), Row, *Style.Appearance,
				*Table.GetCell(Row, TEXT("Likelihood")), *Table.GetCell(Row, TEXT("ScaleMin")), *Table.GetCell(Row, TEXT("ScaleMax")), *Table.GetCell(Row, TEXT("SpawnRange")), *Table.GetCell(Row, TEXT("DeleteRange")));
			return false;
		}
		Parsed.Add(MoveTemp(Style));
	}
	Out = MoveTemp(Parsed);
	return true;
}
