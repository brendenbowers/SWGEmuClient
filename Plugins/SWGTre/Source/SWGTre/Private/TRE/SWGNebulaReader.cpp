#include "TRE/SWGNebulaReader.h"
#include "TRE/SWGDataTableReader.h"

namespace
{
	// Upper clamp for authored colour channels; the retail tables go above 1 (and one lightning ramp to 55).
	constexpr float MaxColourValue = 8.f;
}

bool FSWGNebulaReader::Read(const FSWGDataTableData& Table, TArray<FSWGNebulaData>& Out)
{
	if (Table.Rows.Num() > 4096) return false;
	const TCHAR* Fields[] = {TEXT("x"),TEXT("y"),TEXT("z"),TEXT("radius"),TEXT("density"),TEXT("facingPercent"),
		TEXT("facingA"),TEXT("facingR"),TEXT("facingG"),TEXT("facingB"),TEXT("orientedA"),TEXT("orientedR"),TEXT("orientedG"),TEXT("orientedB")};
	for (const TCHAR* Field : Fields)
	{
		const int32 Index = Table.GetColumnIndex(Field);
		if (!Table.ColumnTypes.IsValidIndex(Index) || Table.ColumnTypes[Index] != TEXT('f')) return false;
	}
	const TCHAR* LightningFields[] = {TEXT("lightningFrequency"),TEXT("lightningDurationMax"),
		TEXT("lightningA"),TEXT("lightningR"),TEXT("lightningG"),TEXT("lightningB"),
		TEXT("lightningRampA"),TEXT("lightningRampR"),TEXT("lightningRampG"),TEXT("lightningRampB")};
	for (const TCHAR* Field : LightningFields)
	{
		const int32 Index = Table.GetColumnIndex(Field);
		if (!Table.ColumnTypes.IsValidIndex(Index) || Table.ColumnTypes[Index] != TEXT('f')) return false;
	}
	// "lightningAppearace" is misspelled in the retail table.
	const TCHAR* TextFields[] = {TEXT("name"),TEXT("lightningAppearace")};
	for (const TCHAR* Field : TextFields)
	{
		const int32 Index = Table.GetColumnIndex(Field);
		if (!Table.ColumnTypes.IsValidIndex(Index) || Table.ColumnTypes[Index] != TEXT('s')) return false;
	}
	TArray<FSWGNebulaData> Parsed;
	for (int32 Row = 0; Row < Table.Rows.Num(); ++Row)
	{
		float Values[UE_ARRAY_COUNT(Fields)];
		for (int32 Field = 0; Field < UE_ARRAY_COUNT(Fields); ++Field)
			if (!LexTryParseString(Values[Field],*Table.GetCell(Row,Fields[Field])) || !FMath::IsFinite(Values[Field])) return false;
		// Retail colour channels run past 1 (space_lok has 1.4, one lightning ramp 55): clamp rather than refuse the zone.
		for (int32 Field = 6; Field < UE_ARRAY_COUNT(Fields); ++Field) Values[Field] = FMath::Clamp(Values[Field], 0.f, MaxColourValue);
		FSWGNebulaData Data;
		Data.Name = Table.GetCell(Row,TEXT("name"));
		Data.NativePosition = FVector(Values[0],Values[1],Values[2]);
		Data.Radius = Values[3]; Data.Density = Values[4]; Data.FacingPercent = Values[5];
		Data.FacingColor = FLinearColor(Values[7],Values[8],Values[9],Values[6]);
		Data.OrientedColor = FLinearColor(Values[11],Values[12],Values[13],Values[10]);
		if (Data.Name.IsEmpty() || Data.Radius < .001f || Data.Radius > 1.e6f || Data.Density > 1.e6f || Data.NativePosition.GetAbsMax() > 1.e6
			|| Data.FacingPercent < 0 || Data.FacingPercent > 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGNebulaReader: row %d (%s) has a value out of range: radius %g density %g facingPercent %g"), Row, *Data.Name, Data.Radius, Data.Density, Data.FacingPercent);
			return false;
		}
		float Lightning[UE_ARRAY_COUNT(LightningFields)];
		for (int32 Field = 0; Field < UE_ARRAY_COUNT(LightningFields); ++Field)
			if (!LexTryParseString(Lightning[Field],*Table.GetCell(Row,LightningFields[Field])) || !FMath::IsFinite(Lightning[Field])) return false;
		for (int32 Field = 2; Field < UE_ARRAY_COUNT(LightningFields); ++Field) Lightning[Field] = FMath::Clamp(Lightning[Field], 0.f, MaxColourValue);
		Data.LightningFrequency = Lightning[0]; Data.LightningDurationMax = Lightning[1];
		Data.LightningColor = FLinearColor(Lightning[3],Lightning[4],Lightning[5],Lightning[2]);
		Data.LightningRampColor = FLinearColor(Lightning[7],Lightning[8],Lightning[9],Lightning[6]);
		Data.LightningAppearance = Table.GetCell(Row,TEXT("lightningAppearace"));
		if (Data.LightningFrequency < 0 || Data.LightningFrequency > 100 || Data.LightningDurationMax < 0 || Data.LightningDurationMax > 600)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGNebulaReader: row %d (%s) lightning frequency %g or duration %g out of range"), Row, *Data.Name, Data.LightningFrequency, Data.LightningDurationMax);
			return false;
		}
		Parsed.Add(MoveTemp(Data));
	}
	Out = MoveTemp(Parsed);
	return true;
}
