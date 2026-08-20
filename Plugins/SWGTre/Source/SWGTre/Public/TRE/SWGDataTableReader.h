#pragma once

#include "CoreMinimal.h"
class FSWGIffReader;

/** One row's cells, in the same order as FSWGDataTableData::ColumnNames — already formatted to string regardless of the column's wire type. */
struct FSWGDataTableRow
{
	TArray<FString> Cells;
};

/**
 * Generic, engine-agnostic decode of one SWG DataTable (.iff, FORM DTII) —
 * the format behind datatables/**\/*.iff: FORM DTII > FORM 0001 > CHUNK COLS/TYPE/ROWS
 */
struct FSWGDataTableData
{
	TArray<FString> ColumnNames;
	TArray<TCHAR> ColumnTypes; // parallel to ColumnNames
	TArray<FSWGDataTableRow> Rows;

	int32 GetColumnIndex(const FString& ColumnName) const
	{
		return ColumnNames.IndexOfByKey(ColumnName);
	}

	/** Cell value for RowIdx/ColumnName, or an empty string if either index is out of range. */
	FString GetCell(int32 RowIdx, const FString& ColumnName) const
	{
		const int32 ColIdx = GetColumnIndex(ColumnName);
		if (!Rows.IsValidIndex(RowIdx) || !Rows[RowIdx].Cells.IsValidIndex(ColIdx))
		{
			return FString();
		}
		return Rows[RowIdx].Cells[ColIdx];
	}

	/** First row whose ColumnName cell equals Value (e.g. a "doorStyleName"/"name" key column), or nullptr. */
	const FSWGDataTableRow* FindRowByColumn(const FString& ColumnName, const FString& Value) const
	{
		const int32 ColIdx = GetColumnIndex(ColumnName);
		if (ColIdx == INDEX_NONE)
		{
			return nullptr;
		}
		return Rows.FindByPredicate([ColIdx, &Value](const FSWGDataTableRow& Row)
			{
				return Row.Cells.IsValidIndex(ColIdx) && Row.Cells[ColIdx] == Value;
			});
	}
};

class SWGTRE_API FSWGDataTableReader
{
public:
	static bool ReadDataTable(const FSWGIffReader& Reader, FSWGDataTableData& OutData);

private:
	FSWGDataTableReader() = default;
};
