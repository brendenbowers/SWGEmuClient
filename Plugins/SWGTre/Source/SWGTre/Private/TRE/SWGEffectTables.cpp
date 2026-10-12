#include "TRE/SWGEffectTables.h"

namespace
{
	FString ToggleSharedPrefix(const FString& TemplatePath)
	{
		int32 SlashIndex = INDEX_NONE;
		TemplatePath.FindLastChar(TEXT('/'), SlashIndex);
		const FString Directory = TemplatePath.Left(SlashIndex + 1);
		FString FileName = TemplatePath.Mid(SlashIndex + 1);
		if (!FileName.RemoveFromStart(TEXT("shared_")))
		{
			FileName = TEXT("shared_") + FileName;
		}
		return Directory + FileName;
	}
}

FString FSWGEffectTables::FindProjectileAppearance(const FSWGDataTableData& WeaponTable, const FString& EffectId, int32 EffectIndex)
{
	for (int32 RowIndex = 0; RowIndex < WeaponTable.Rows.Num(); ++RowIndex)
	{
		if (WeaponTable.GetCell(RowIndex, TEXT("Weapon Effect Id")) == EffectId
			&& WeaponTable.GetCell(RowIndex, TEXT("Weapon Effect Index")) == FString::FromInt(EffectIndex))
		{
			FString Projectile = WeaponTable.GetCell(RowIndex, TEXT("Projectile Appearance Template"));
			Projectile.ReplaceInline(TEXT("\\"), TEXT("/"));
			return Projectile;
		}
	}
	return FString();
}

bool FSWGEffectTables::FindSwoosh(const FSWGDataTableData& SwooshTable, const FString& ObjectTemplatePath, FString& OutSwooshPath, bool& bOutUseObjectHue)
{
	for (const FString& Candidate : { ObjectTemplatePath, ToggleSharedPrefix(ObjectTemplatePath) })
	{
		if (const FSWGDataTableRow* Row = SwooshTable.FindRowByColumn(TEXT("ObjectTemplateName"), Candidate))
		{
			const int32 SwooshColumn = SwooshTable.GetColumnIndex(TEXT("SwooshName"));
			const int32 HueColumn = SwooshTable.GetColumnIndex(TEXT("UseObjectHue"));
			if (!Row->Cells.IsValidIndex(SwooshColumn))
			{
				return false;
			}
			OutSwooshPath = Row->Cells[SwooshColumn];
			OutSwooshPath.ReplaceInline(TEXT("\\"), TEXT("/"));
			bOutUseObjectHue = Row->Cells.IsValidIndex(HueColumn) && FCString::Atoi(*Row->Cells[HueColumn]) != 0;
			return !OutSwooshPath.IsEmpty();
		}
	}
	return false;
}

bool FSWGEffectTables::FindSwooshHueColor(const FSWGDataTableData& HueTable, float HueDegrees, FColor& OutColor)
{
	for (int32 RowIndex = 0; RowIndex < HueTable.Rows.Num(); ++RowIndex)
	{
		const float HueMin = FCString::Atof(*HueTable.GetCell(RowIndex, TEXT("HueMin")));
		const float HueMax = FCString::Atof(*HueTable.GetCell(RowIndex, TEXT("HueMax")));
		if (HueDegrees >= HueMin && HueDegrees < HueMax)
		{
			OutColor = FColor(
				(uint8)FCString::Atoi(*HueTable.GetCell(RowIndex, TEXT("Red"))),
				(uint8)FCString::Atoi(*HueTable.GetCell(RowIndex, TEXT("Green"))),
				(uint8)FCString::Atoi(*HueTable.GetCell(RowIndex, TEXT("Blue"))));
			return true;
		}
	}
	return false;
}
