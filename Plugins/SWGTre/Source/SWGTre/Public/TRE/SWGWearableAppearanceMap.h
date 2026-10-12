#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGDataTableReader.h"

/**
 * datatables/appearance/wearable_appearance_map.iff (retail WearableAppearanceMap, SWGEmu.exe
 * 0x11b9f70 install, 0x11b9b80 load, 0x11b9ff0 lookup). Columns: source_wearable_appearance_name,
 * wearer_appearance_name, mapped_wearable_appearance_name. The lookup is a binary search on
 * (source, wearer); an empty mapped name means the wearable is not shown on that wearer.
 *
 * The retail client only asks it one question: the mount code (0x647e20, run from 0x4369a0 when a
 * creature's appearance is set up) looks up ("lookup/mnt_wr_default_covering", mount appearance) and
 * wears the answer, the saddle blanket cut for that mount's skeleton, on the mount.
 */
class SWGTRE_API FSWGWearableAppearanceMap
{
public:
	/** The source key the mount code passes (the string at 0x1883d78). */
	static const TCHAR* DefaultCoveringSource();

	/** Builds the lookup from the generic table; false when a column is missing. */
	static bool Read(const FSWGDataTableData& Table, FSWGWearableAppearanceMap& OutResult);

	enum class EResult : uint8
	{
		NotFound,
		/** An entry exists but names no appearance: the wearable is hidden on this wearer. */
		Hidden,
		Mapped,
	};

	/** Keys compare case-insensitively. OutMapped is set only for Mapped. */
	EResult Find(const FString& SourceAppearance, const FString& WearerAppearance, FString& OutMapped) const;

	int32 Num() const { return Entries.Num(); }

	struct FEntry
	{
		FString Source;
		FString Wearer;
		FString Mapped;
	};

	const TArray<FEntry>& GetEntries() const { return Entries; }

private:
	TArray<FEntry> Entries;
	TMap<FString, int32> IndexByKey;
};
