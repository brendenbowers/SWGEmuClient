#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGIFFChunkReader.h"

bool FSWGDataTableReader::ReadDataTable(const FSWGIffReader& Reader, FSWGDataTableData& OutData)
{
	OutData.ColumnNames.Reset();
	OutData.ColumnTypes.Reset();
	OutData.Rows.Reset();

	FSWGIffChunk DtiiForm;
	if (!Reader.FindForm(SWG_IFF_TAG('D', 'T', 'I', 'I'), DtiiForm))
	{
		return false;
	}

	TArray<FSWGIffChunk> DtiiChildren = Reader.ReadChildren(DtiiForm);
	if (DtiiChildren.Num() == 0 || !DtiiChildren[0].IsForm())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGDataTableReader: DTII has no version FORM"));
		return false;
	}
	const FSWGIffChunk& VersionForm = DtiiChildren[0]; // e.g. FORM 0001

	FSWGIffChunk ColsChunk, TypeChunk, RowsChunk;
	if (!Reader.FindChildChunk(VersionForm, SWG_IFF_TAG('C', 'O', 'L', 'S'), ColsChunk)
		|| !Reader.FindChildChunk(VersionForm, SWG_IFF_TAG('T', 'Y', 'P', 'E'), TypeChunk)
		|| !Reader.FindChildChunk(VersionForm, SWG_IFF_TAG('R', 'O', 'W', 'S'), RowsChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGDataTableReader: version form missing COLS/TYPE/ROWS"));
		return false;
	}

	int32 ColumnCount = 0;
	{
		FSWGIFFChunkReader ColsReader(ColsChunk, Reader);
		if (!ColsReader.ReadValueLE(ColumnCount))
		{
			return false;
		}
		OutData.ColumnNames.Reserve(ColumnCount);
		for (int32 i = 0; i < ColumnCount; ++i)
		{
			FString Name;
			if (!ColsReader.ReadTerminiatedString(Name))
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGDataTableReader: COLS truncated at column %d of %d"), i, ColumnCount);
				return false;
			}
			OutData.ColumnNames.Add(MoveTemp(Name));
		}
	}

	{
		FSWGIFFChunkReader TypeReader(TypeChunk, Reader);
		OutData.ColumnTypes.Reserve(ColumnCount);
		for (int32 i = 0; i < ColumnCount; ++i)
		{
			FString TypeName;
			if (!TypeReader.ReadTerminiatedString(TypeName) || TypeName.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGDataTableReader: TYPE truncated at column %d of %d"), i, ColumnCount);
				return false;
			}
			// Only the first character matters (Core3's own comment: "Default value doesn't seem to matter").
			OutData.ColumnTypes.Add(TypeName[0]);
		}
	}

	{
		FSWGIFFChunkReader RowsReader(RowsChunk, Reader);
		int32 RowCount = 0;
		if (!RowsReader.ReadValueLE(RowCount))
		{
			return false;
		}
		OutData.Rows.Reserve(RowCount);
		for (int32 r = 0; r < RowCount; ++r)
		{
			FSWGDataTableRow& Row = OutData.Rows.AddDefaulted_GetRef();
			Row.Cells.Reserve(ColumnCount);
			for (int32 c = 0; c < ColumnCount; ++c)
			{
				const TCHAR Type = OutData.ColumnTypes[c];
				if (Type == TEXT('s'))
				{
					FString Value;
					if (!RowsReader.ReadTerminiatedString(Value))
					{
						UE_LOG(LogTemp, Warning, TEXT("FSWGDataTableReader: ROWS truncated at row %d column %d (string)"), r, c);
						return false;
					}
					Row.Cells.Add(MoveTemp(Value));
				}
				else if (Type == TEXT('f'))
				{
					float Value = 0.0f;
					if (!RowsReader.ReadValueLE(Value))
					{
						UE_LOG(LogTemp, Warning, TEXT("FSWGDataTableReader: ROWS truncated at row %d column %d (float)"), r, c);
						return false;
					}
					Row.Cells.Add(FString::SanitizeFloat(Value));
				}
				else
				{
					// 'i'/'c'/'p'/'e'/'z'/'I'/'h'/'b' — all a plain 4-byte int on
					// the wire (Core3's DataTableCellBinary::parse reads a full
					// int for "binary"/bool columns too, not a single byte).
					int32 Value = 0;
					if (!RowsReader.ReadValueLE(Value))
					{
						UE_LOG(LogTemp, Warning, TEXT("FSWGDataTableReader: ROWS truncated at row %d column %d (int, type '%c')"), r, c, Type);
						return false;
					}
					Row.Cells.Add(FString::FromInt(Value));
				}
			}
		}
	}

	return true;
}
