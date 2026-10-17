#include "TRE/SWGCustomizationTables.h"

namespace
{
	/** Cell of Row at the named column; empty when the column is absent. */
	FString Cell(const FSWGDataTableData& Table, const FSWGDataTableRow& Row, const TCHAR* Column)
	{
		const int32 Index = Table.GetColumnIndex(Column);
		return Row.Cells.IsValidIndex(Index) ? Row.Cells[Index] : FString();
	}

	bool CellBool(const FSWGDataTableData& Table, const FSWGDataTableRow& Row, const TCHAR* Column, bool bDefault)
	{
		const FString Value = Cell(Table, Row, Column);
		return Value.IsEmpty() ? bDefault : FCString::Atoi(*Value) != 0;
	}

	int32 CellInt(const FSWGDataTableData& Table, const FSWGDataTableRow& Row, const TCHAR* Column)
	{
		return FCString::Atoi(*Cell(Table, Row, Column));
	}
}

bool FSWGAllowBaldTable::Read(const FSWGDataTableData& Table, FSWGAllowBaldTable& OutResult)
{
	OutResult.BySpeciesGender.Reset();
	if (Table.GetColumnIndex(TEXT("SPECIES_GENDER")) == INDEX_NONE || Table.GetColumnIndex(TEXT("ALLOW_BALD")) == INDEX_NONE)
	{
		return false;
	}
	for (const FSWGDataTableRow& Row : Table.Rows)
	{
		OutResult.BySpeciesGender.Add(Cell(Table, Row, TEXT("SPECIES_GENDER")), CellBool(Table, Row, TEXT("ALLOW_BALD"), true));
	}
	return true;
}

bool FSWGAllowBaldTable::IsBaldAllowed(const FString& SpeciesGender) const
{
	const bool* Allowed = BySpeciesGender.Find(SpeciesGender);
	return Allowed ? *Allowed : true;
}

bool FSWGCustomizationGroupTable::Read(const FSWGDataTableData& SharedTable, const FSWGDataTableData& InstanceTable, FSWGCustomizationGroupTable& OutResult)
{
	OutResult.Shared.Reset();
	OutResult.Instances.Reset();
	if (SharedTable.GetColumnIndex(TEXT("CUSTOMIZATION_GROUP")) == INDEX_NONE || InstanceTable.GetColumnIndex(TEXT("SPECIES_GENDER")) == INDEX_NONE)
	{
		return false;
	}

	for (const FSWGDataTableRow& Row : SharedTable.Rows)
	{
		FSWGCustomizationGroupShared& Shared = OutResult.Shared.AddDefaulted_GetRef();
		Shared.Group = Cell(SharedTable, Row, TEXT("CUSTOMIZATION_GROUP"));
		Shared.bIsMarking = CellBool(SharedTable, Row, TEXT("IS_MARKING"), false);
		Shared.bIsHair = CellBool(SharedTable, Row, TEXT("IS_HAIR"), false);
	}
	for (const FSWGDataTableRow& Row : InstanceTable.Rows)
	{
		FSWGCustomizationGroupInstance& Instance = OutResult.Instances.AddDefaulted_GetRef();
		Instance.SpeciesGender = Cell(InstanceTable, Row, TEXT("SPECIES_GENDER"));
		Instance.Group = Cell(InstanceTable, Row, TEXT("CUSTOMIZATION_GROUP"));
		Instance.bColorLinkedToHair = CellBool(InstanceTable, Row, TEXT("IS_COLOR_LINKED_TO_HAIR"), false);
		Instance.bColorLinkedToBody = CellBool(InstanceTable, Row, TEXT("IS_COLOR_LINKED_TO_BODY"), false);
		Instance.LookAtBone = Cell(InstanceTable, Row, TEXT("LOOK_AT_BONE"));
		Instance.ZoomFactor = FCString::Atof(*Cell(InstanceTable, Row, TEXT("ZOOM_FACTOR")));
	}
	return true;
}

const FSWGCustomizationGroupShared* FSWGCustomizationGroupTable::FindShared(const FString& Group) const
{
	return Shared.FindByPredicate([&Group](const FSWGCustomizationGroupShared& Candidate) { return Candidate.Group.Equals(Group, ESearchCase::IgnoreCase); });
}

TArray<const FSWGCustomizationGroupInstance*> FSWGCustomizationGroupTable::GetGroupsFor(const FString& SpeciesGender) const
{
	TArray<const FSWGCustomizationGroupInstance*> Result;
	for (const FSWGCustomizationGroupInstance& Instance : Instances)
	{
		if (Instance.SpeciesGender.Equals(SpeciesGender, ESearchCase::IgnoreCase))
		{
			Result.Add(&Instance);
		}
	}
	return Result;
}

bool FSWGCustomizationDataTable::Read(const FSWGDataTableData& Table, FSWGCustomizationDataTable& OutResult)
{
	OutResult.Rows.Reset();
	if (Table.GetColumnIndex(TEXT("SPECIES_GENDER")) == INDEX_NONE || Table.GetColumnIndex(TEXT("CUSTOMIZATION_NAME")) == INDEX_NONE)
	{
		return false;
	}

	for (const FSWGDataTableRow& Row : Table.Rows)
	{
		FSWGCustomizationDataRow& Out = OutResult.Rows.AddDefaulted_GetRef();
		Out.SpeciesGender = Cell(Table, Row, TEXT("SPECIES_GENDER"));
		Out.Group = Cell(Table, Row, TEXT("CUSTOMIZATION_GROUP"));
		Out.Type = Cell(Table, Row, TEXT("TYPE"));
		Out.Name = Cell(Table, Row, TEXT("CUSTOMIZATION_NAME"));
		Out.Variables = Cell(Table, Row, TEXT("VARIABLES"));
		Out.bIsScale = CellBool(Table, Row, TEXT("IS_SCALE"), false);
		Out.bReverse = CellBool(Table, Row, TEXT("REVERSE"), false);
		Out.bDiscrete = CellBool(Table, Row, TEXT("DISCRETE"), false);
		Out.bRandomizable = CellBool(Table, Row, TEXT("RAMDOMIZABLE"), true); // sic: the retail column name
		Out.bIsVarHairColor = CellBool(Table, Row, TEXT("IS_VAR_HAIR_COLOR"), false);
		Out.ImageDesignSkillMod = Cell(Table, Row, TEXT("IMAGEDESIGN_SKILLMOD"));
		Out.SkillModValue = CellInt(Table, Row, TEXT("SKILLMOD_VALUE"));
		Out.CameraYaw = FCString::Atof(*Cell(Table, Row, TEXT("CAMERA_YAW")));
		for (int32 Column = 0; Column < Table.ColumnNames.Num() && Column < Row.Cells.Num(); ++Column)
		{
			Out.Cells.Add(Table.ColumnNames[Column], Row.Cells[Column]);
		}
	}
	return true;
}

TArray<const FSWGCustomizationDataRow*> FSWGCustomizationDataTable::GetRowsFor(const FString& SpeciesGender, const FString& Group) const
{
	TArray<const FSWGCustomizationDataRow*> Result;
	for (const FSWGCustomizationDataRow& Row : Rows)
	{
		if (Row.SpeciesGender.Equals(SpeciesGender, ESearchCase::IgnoreCase) && (Group.IsEmpty() || Row.Group.Equals(Group, ESearchCase::IgnoreCase)))
		{
			Result.Add(&Row);
		}
	}
	return Result;
}

bool FSWGPaletteColumnsTable::Read(const FSWGDataTableData& Table, FSWGPaletteColumnsTable& OutResult)
{
	OutResult.Rows.Reset();
	if (Table.GetColumnIndex(TEXT("PALETTE")) == INDEX_NONE || Table.GetColumnIndex(TEXT("CREATION_COLUMNS")) == INDEX_NONE)
	{
		return false;
	}

	static const TCHAR* const TierNames[] = { TEXT("NOVICE"), TEXT("LEVEL1"), TEXT("LEVEL2"), TEXT("LEVEL3"), TEXT("LEVEL4"), TEXT("MASTER") };
	for (const FSWGDataTableRow& Row : Table.Rows)
	{
		FSWGPaletteColumns& Out = OutResult.Rows.AddDefaulted_GetRef();
		Out.Palette = Cell(Table, Row, TEXT("PALETTE"));
		Out.CreationColumns = CellInt(Table, Row, TEXT("CREATION_COLUMNS"));
		Out.CreationIndexes = CellInt(Table, Row, TEXT("CREATION_INDEXES"));
		for (const TCHAR* Tier : TierNames)
		{
			Out.ImageDesignTiers.Emplace(
				CellInt(Table, Row, *FString::Printf(TEXT("ID_%s_COLUMNS"), Tier)),
				CellInt(Table, Row, *FString::Printf(TEXT("ID_%s_INDEXES"), Tier)));
		}
	}
	return true;
}

const FSWGPaletteColumns* FSWGPaletteColumnsTable::Find(const FString& PaletteName) const
{
	return Rows.FindByPredicate([&PaletteName](const FSWGPaletteColumns& Candidate) { return Candidate.Palette.Equals(PaletteName, ESearchCase::IgnoreCase); });
}

bool FSWGHairAssetTable::Read(const FSWGDataTableData& Table, FSWGHairAssetTable& OutResult)
{
	OutResult.Rows.Reset();
	if (Table.GetColumnIndex(TEXT("SHARED_TEMPLATE")) == INDEX_NONE)
	{
		return false;
	}
	for (const FSWGDataTableRow& Row : Table.Rows)
	{
		FSWGHairAsset& Out = OutResult.Rows.AddDefaulted_GetRef();
		Out.SharedTemplate = Cell(Table, Row, TEXT("SHARED_TEMPLATE"));
		Out.PlayerTemplate = Cell(Table, Row, TEXT("PLAYER_TEMPLATE"));
		Out.SkillModValue = CellInt(Table, Row, TEXT("SKILL_MOD_VALUE"));
		Out.bAvailableAtCreation = CellBool(Table, Row, TEXT("AVAILABLE_AT_CREATION"), false);
	}
	return true;
}
