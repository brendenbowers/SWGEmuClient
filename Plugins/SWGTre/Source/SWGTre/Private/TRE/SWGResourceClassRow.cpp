#include "TRE/SWGResourceClassRow.h"
#include "TRE/SWGDataTableReader.h"
#include "UObject/Package.h"

namespace SWGResourceClass
{
	const FString DataTablePath = TEXT("/Game/SWGEmu/Generated/DT_ResourceClasses.DT_ResourceClasses");
}

UDataTable* SWGResourceClass::BuildDataTable(const FSWGDataTableData& RawData)
{
	const int32 EnumCol = RawData.GetColumnIndex(TEXT("ENUM"));
	if (EnumCol == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("SWGResourceClass::BuildDataTable: resource_tree.iff has no ENUM column"));
		return nullptr;
	}

	TArray<int32> ClassCols;
	for (int32 Depth = 1; Depth <= 8; ++Depth)
	{
		const int32 Col = RawData.GetColumnIndex(FString::Printf(TEXT("CLASS %d"), Depth));
		if (Col != INDEX_NONE)
		{
			ClassCols.Add(Col);
		}
	}
	if (ClassCols.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("SWGResourceClass::BuildDataTable: resource_tree.iff has no CLASS columns"));
		return nullptr;
	}

	const FString PackagePath = FPackageName::ObjectPathToPackageName(DataTablePath);
	UPackage* Package = CreatePackage(*PackagePath);
	const FString ObjectName = FPackageName::ObjectPathToObjectName(DataTablePath);

	UDataTable* Table = NewObject<UDataTable>(Package, FName(*ObjectName), RF_Public | RF_Standalone);
	Table->RowStruct = FSWGResourceClassRow::StaticStruct();

	// EnumAtDepth[d] is the most recent row seen at depth d, which is exactly
	// the parent for the next row at depth d+1.
	TArray<FString> EnumAtDepth;
	EnumAtDepth.SetNum(ClassCols.Num() + 1);

	for (const FSWGDataTableRow& RawRow : RawData.Rows)
	{
		if (!RawRow.Cells.IsValidIndex(EnumCol) || RawRow.Cells[EnumCol].IsEmpty())
		{
			continue;
		}

		int32 Depth = INDEX_NONE;
		for (int32 i = 0; i < ClassCols.Num(); ++i)
		{
			if (RawRow.Cells.IsValidIndex(ClassCols[i]) && !RawRow.Cells[ClassCols[i]].IsEmpty())
			{
				Depth = i + 1;
				break;
			}
		}
		if (Depth == INDEX_NONE)
		{
			continue;
		}

		const FString& Enum = RawRow.Cells[EnumCol];
		EnumAtDepth[Depth] = Enum;

		FSWGResourceClassRow Row;
		Row.ParentClass = Depth > 1 ? EnumAtDepth[Depth - 1] : FString();
		Row.DisplayName = RawRow.Cells[ClassCols[Depth - 1]];

		Table->AddRow(FName(*Enum), Row);
	}

	return Table;
}

TArray<FString> SWGResourceClass::GetIconCandidates(const UDataTable* Table, const FString& ResourceType)
{
	TArray<FString> Candidates;
	if (!Table || ResourceType.IsEmpty())
	{
		return Candidates;
	}

	FString Current = ResourceType;
	TSet<FString> Visited;
	while (!Current.IsEmpty() && !Visited.Contains(Current))
	{
		Visited.Add(Current);

		const FSWGResourceClassRow* Row = Table->FindRow<FSWGResourceClassRow>(FName(*Current), TEXT("SWGResourceClass::GetIconCandidates"), false);
		const FString Parent = Row ? Row->ParentClass : FString();

		Candidates.AddUnique(Current);
		if (!Parent.IsEmpty() && !Current.StartsWith(Parent + TEXT("_")))
		{
			Candidates.AddUnique(Parent + TEXT("_") + Current);
		}

		Current = Parent;
	}

	return Candidates;
}
