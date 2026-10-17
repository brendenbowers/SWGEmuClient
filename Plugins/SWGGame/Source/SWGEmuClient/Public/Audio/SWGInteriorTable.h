#pragma once

#include "CoreMinimal.h"

class ASWGCell;
class UGameInstance;
class USWGTreSubsystem;

/** One row of datatables/interior/interior.iff: how a room sounds and feels underfoot. */
struct FSWGInteriorRow
{
	FString PobName;
	FString CellName;
	FString DayAmbient;
	FString NightAmbient;
	FString FirstMusic;
	FString SurfaceType;
	/** Reverb preset enum (see SWGReverbPresets). */
	int32 RoomType = 0;
};

/**
 * interior.iff by POB base name and POB cell name (the CELL chunk's name, "lobby", "spawn", not the
 * server's room label). A building without its own row for a room falls back to its "default" row, then
 * to the table's global default row. Game thread only.
 */
class SWGEMUCLIENT_API FSWGInteriorTable
{
public:
	enum class EMatch : uint8
	{
		None,
		Cell,
		BuildingDefault,
		GlobalDefault,
	};

	/** Loaded once per process; null when the table cannot be read. */
	static const FSWGInteriorTable* Get(const USWGTreSubsystem& Tre);

	/** Case-insensitive. Null only if the table is empty. */
	const FSWGInteriorRow* Find(const FString& PobName, const FString& CellName, EMatch* OutMatch = nullptr) const;

	const TArray<FSWGInteriorRow>& GetRows() const { return Rows; }

	/** POB base name and POB cell name of a room. False until its building's template and POB are known. */
	static bool ResolveCellKey(const ASWGCell* Cell, UGameInstance& GameInstance, FString& OutPobName, FString& OutCellName);

	/** Find() for a room actor; null when its key cannot be resolved. */
	static const FSWGInteriorRow* FindForCell(const ASWGCell* Cell, UGameInstance& GameInstance, EMatch* OutMatch = nullptr);

	static const TCHAR* MatchName(EMatch Match);

private:
	TArray<FSWGInteriorRow> Rows;
};
