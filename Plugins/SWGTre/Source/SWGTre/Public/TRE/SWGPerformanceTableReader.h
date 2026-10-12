#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** datatables/performance/performance.iff "type" column, as the hash stored in the file. */
enum class ESWGPerformanceKind : uint8
{
	Unknown,
	Music,
	Dance,
};

/**
 * One performance.iff row. The server's performance index (CREO base6/delta6 PerformanceType, and the
 * 0x166 flourish owner's current song) is the 1-based row number, so Index is RowIndex + 1.
 * A song is one row per instrument: Song + InstrumentAudioId pick the row, and each row names its own samples.
 */
struct FSWGPerformanceRow
{
	int32 Index = 0;
	/** performanceName: the song or dance name ("starwars1", "ballad", "tumble"). */
	FString Name;
	/** instrumentAudioId: 1 traz, 2 slitherhorn, 3 fanfar, 4 flutedroopy, 5 kloohorn, 6 fizz, 7 bandfill, 8 omnibox, 9 nalargon, 10 mandoviol. 0 for a dance. */
	int32 InstrumentAudioId = 0;
	/** requiredSong: the ability that unlocks it ("startMusic+ballad"). */
	FString RequiredSong;
	/** requiredInstrument: the instrument's name ("slitherhorn"); empty for a dance. */
	FString RequiredInstrument;
	/** requiredDance: the ability that unlocks a dance ("startDance+tumble"); empty for music. */
	FString RequiredDance;
	int32 DanceVisualId = 0;
	int32 ActionPointsPerLoop = 0;
	/** loopDuration: seconds one mainloop/flourish iteration lasts (5, 5.81, 6.4 or 6.43). */
	float LoopDurationSeconds = 0.f;
	ESWGPerformanceKind Kind = ESWGPerformanceKind::Unknown;
	int32 BaseXp = 0;
	int32 FlourishXpMod = 0;
	int32 HealMindWound = 0;
	int32 HealShockWound = 0;
	FString RequiredSkillMod;
	int32 RequiredSkillModValue = 0;
	/** The .snd files; all empty for a dance, which has no audio. */
	FString MainLoop;
	FString Flourish[8];
	FString Intro;
	FString Outro;

	bool IsMusic() const { return Kind == ESWGPerformanceKind::Music; }
};

/** The whole table in file order. */
struct FSWGPerformanceTable
{
	TArray<FSWGPerformanceRow> Rows;

	/** By the server's 1-based performance index; null for 0 or out of range. */
	const FSWGPerformanceRow* FindByIndex(int32 PerformanceIndex) const
	{
		return Rows.IsValidIndex(PerformanceIndex - 1) ? &Rows[PerformanceIndex - 1] : nullptr;
	}

	/** A song by name and instrumentAudioId (case-insensitive name), or null. */
	const FSWGPerformanceRow* FindSong(const FString& SongName, int32 InstrumentAudioId) const
	{
		return Rows.FindByPredicate([&](const FSWGPerformanceRow& Row)
			{
				return Row.IsMusic() && Row.InstrumentAudioId == InstrumentAudioId && Row.Name.Equals(SongName, ESearchCase::IgnoreCase);
			});
	}

	/** instrumentAudioId for an instrument name ("slitherhorn"), or 0 if no music row uses it. */
	int32 FindInstrumentAudioId(const FString& InstrumentName) const
	{
		const FSWGPerformanceRow* Row = Rows.FindByPredicate([&](const FSWGPerformanceRow& Candidate)
			{
				return Candidate.IsMusic() && Candidate.RequiredInstrument.Equals(InstrumentName, ESearchCase::IgnoreCase);
			});
		return Row ? Row->InstrumentAudioId : 0;
	}
};

class SWGTRE_API FSWGPerformanceTableReader
{
public:
	static bool Read(const FSWGIffReader& Reader, FSWGPerformanceTable& OutTable);
};
