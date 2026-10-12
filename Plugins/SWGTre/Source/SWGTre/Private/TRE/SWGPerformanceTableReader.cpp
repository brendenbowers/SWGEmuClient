#include "TRE/SWGPerformanceTableReader.h"

#include "TRE/SWGDataTableReader.h"

namespace
{
	// Stored in the "type" column as int32 hashes of the type names.
	constexpr int32 MusicTypeHash = 866729052;
	constexpr int32 DanceTypeHash = -1788534963;
}

bool FSWGPerformanceTableReader::Read(const FSWGIffReader& Reader, FSWGPerformanceTable& OutTable)
{
	OutTable.Rows.Reset();

	FSWGDataTableData Table;
	if (!FSWGDataTableReader::ReadDataTable(Reader, Table) || Table.GetColumnIndex(TEXT("mainloop")) == INDEX_NONE)
	{
		return false;
	}

	for (int32 RowIndex = 0; RowIndex < Table.Rows.Num(); ++RowIndex)
	{
		auto Cell = [&](const TCHAR* Column) { return Table.GetCell(RowIndex, Column); };
		auto IntCell = [&](const TCHAR* Column) { return FCString::Atoi(*Table.GetCell(RowIndex, Column)); };

		FSWGPerformanceRow& Row = OutTable.Rows.AddDefaulted_GetRef();
		Row.Index = RowIndex + 1;
		Row.Name = Cell(TEXT("performanceName"));
		Row.InstrumentAudioId = IntCell(TEXT("instrumentAudioId"));
		Row.RequiredSong = Cell(TEXT("requiredSong"));
		Row.RequiredInstrument = Cell(TEXT("requiredInstrument"));
		Row.RequiredDance = Cell(TEXT("requiredDance"));
		Row.DanceVisualId = IntCell(TEXT("danceVisualId"));
		Row.ActionPointsPerLoop = IntCell(TEXT("actionPointsPerLoop"));
		Row.LoopDurationSeconds = FCString::Atof(*Cell(TEXT("loopDuration")));

		const int32 TypeHash = IntCell(TEXT("type"));
		Row.Kind = TypeHash == MusicTypeHash ? ESWGPerformanceKind::Music : TypeHash == DanceTypeHash ? ESWGPerformanceKind::Dance : ESWGPerformanceKind::Unknown;

		Row.BaseXp = IntCell(TEXT("baseXp"));
		Row.FlourishXpMod = IntCell(TEXT("flourishXpMod"));
		Row.HealMindWound = IntCell(TEXT("healMindWound"));
		Row.HealShockWound = IntCell(TEXT("healShockWound"));
		Row.RequiredSkillMod = Cell(TEXT("requiredSkillMod"));
		Row.RequiredSkillModValue = IntCell(TEXT("requiredSkillModValue"));
		Row.MainLoop = Cell(TEXT("mainloop"));
		for (int32 FlourishIndex = 0; FlourishIndex < 8; ++FlourishIndex)
		{
			Row.Flourish[FlourishIndex] = Table.GetCell(RowIndex, FString::Printf(TEXT("flourish%d"), FlourishIndex + 1));
		}
		Row.Intro = Cell(TEXT("intro"));
		Row.Outro = Cell(TEXT("outro"));
	}

	return !OutTable.Rows.IsEmpty();
}
