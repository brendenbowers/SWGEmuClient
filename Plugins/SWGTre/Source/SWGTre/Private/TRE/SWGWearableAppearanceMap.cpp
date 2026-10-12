#include "TRE/SWGWearableAppearanceMap.h"

namespace
{
	FString MakeKey(const FString& Source, const FString& Wearer)
	{
		return Source.ToLower() + TEXT("|") + Wearer.ToLower();
	}
}

const TCHAR* FSWGWearableAppearanceMap::DefaultCoveringSource()
{
	return TEXT("lookup/mnt_wr_default_covering");
}

bool FSWGWearableAppearanceMap::Read(const FSWGDataTableData& Table, FSWGWearableAppearanceMap& OutResult)
{
	OutResult.Entries.Reset();
	OutResult.IndexByKey.Reset();

	const int32 SourceColumn = Table.GetColumnIndex(TEXT("source_wearable_appearance_name"));
	const int32 WearerColumn = Table.GetColumnIndex(TEXT("wearer_appearance_name"));
	const int32 MappedColumn = Table.GetColumnIndex(TEXT("mapped_wearable_appearance_name"));
	if (SourceColumn == INDEX_NONE || WearerColumn == INDEX_NONE || MappedColumn == INDEX_NONE)
	{
		return false;
	}

	for (const FSWGDataTableRow& Row : Table.Rows)
	{
		FEntry Entry;
		Entry.Source = Row.Cells[SourceColumn];
		Entry.Wearer = Row.Cells[WearerColumn];
		Entry.Mapped = Row.Cells[MappedColumn];
		const FString Key = MakeKey(Entry.Source, Entry.Wearer);
		OutResult.IndexByKey.Add(Key, OutResult.Entries.Add(MoveTemp(Entry)));
	}
	return true;
}

FSWGWearableAppearanceMap::EResult FSWGWearableAppearanceMap::Find(const FString& SourceAppearance, const FString& WearerAppearance, FString& OutMapped) const
{
	const int32* Index = IndexByKey.Find(MakeKey(SourceAppearance, WearerAppearance));
	if (!Index)
	{
		return EResult::NotFound;
	}
	const FString& Mapped = Entries[*Index].Mapped;
	if (Mapped.IsEmpty())
	{
		return EResult::Hidden;
	}
	OutMapped = Mapped;
	return EResult::Mapped;
}
