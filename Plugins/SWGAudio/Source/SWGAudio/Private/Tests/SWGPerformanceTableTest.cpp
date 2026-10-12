#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "Misc/ScopeExit.h"
#include "SWGAudioTypes.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGPerformanceTableReader.h"
#include "TRE/SWGSoundTemplateReader.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Parses datatables/performance/performance.iff and checks that every .snd it names exists, parses
 * and points at a sample that exists. Needs the retail data present.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGPerformanceTableTest,
	"SWGEmu.Audio.Performance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGPerformanceTableTest::RunTest(const FString& Parameters)
{
	UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
	USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
	if (!Tre)
	{
		AddError(TEXT("could not construct a TRE subsystem"));
		return false;
	}

	Tre->AddToRoot();
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };
	Tre->LoadConfig();
	if (!Tre->LoadArchives())
	{
		AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
		return false;
	}

	FSWGPerformanceTable Table;
	if (!FSWGPerformanceTableReader::Read(Tre->CreateIffReader(TEXT("datatables/performance/performance.iff")), Table))
	{
		AddError(TEXT("performance.iff failed to parse"));
		return false;
	}

	int32 MusicRows = 0;
	int32 DanceRows = 0;
	int32 MissingSounds = 0;
	int32 MissingSamples = 0;
	int32 SoundsChecked = 0;
	TSet<FString> Checked;
	TSet<FString> SongInstrumentPairs;

	auto CheckSound = [&](const FSWGPerformanceRow& Row, const TCHAR* Column, const FString& Reference)
	{
		if (Reference.IsEmpty())
		{
			AddError(FString::Printf(TEXT("row %d %s: %s column is empty"), Row.Index, *Row.Name, Column));
			return;
		}
		const FString SoundPath = SWGAudio::NormalizeSoundPath(Reference);
		bool bAlreadyChecked = false;
		Checked.Add(SoundPath, &bAlreadyChecked);
		if (bAlreadyChecked)
		{
			return;
		}
		++SoundsChecked;

		FSWGSoundTemplate Template;
		if (!Tre->FileExists(SoundPath) || !FSWGSoundTemplateReader::Read(Tre->CreateIffReader(SoundPath), Template))
		{
			++MissingSounds;
			AddWarning(FString::Printf(TEXT("row %d %s %s: %s is missing or does not parse"), Row.Index, *Row.Name, Column, *SoundPath));
			return;
		}
		for (const FString& Sample : Template.Samples)
		{
			if (!Tre->FileExists(Sample))
			{
				++MissingSamples;
				AddWarning(FString::Printf(TEXT("row %d %s %s: %s references missing sample %s"), Row.Index, *Row.Name, Column, *SoundPath, *Sample));
			}
		}
	};

	for (int32 RowIndex = 0; RowIndex < Table.Rows.Num(); ++RowIndex)
	{
		const FSWGPerformanceRow& Row = Table.Rows[RowIndex];
		TestEqual(TEXT("Index is the 1-based row number"), Row.Index, RowIndex + 1);
		TestTrue(*FString::Printf(TEXT("row %d has a known type"), Row.Index), Row.Kind != ESWGPerformanceKind::Unknown);

		if (Row.IsMusic())
		{
			++MusicRows;
			TestTrue(*FString::Printf(TEXT("row %d has an instrument id"), Row.Index), Row.InstrumentAudioId > 0);
			TestTrue(*FString::Printf(TEXT("row %d has a loop duration"), Row.Index), Row.LoopDurationSeconds > 0.f);
			bool bDuplicatePair = false;
			SongInstrumentPairs.Add(FString::Printf(TEXT("%s/%d"), *Row.Name.ToLower(), Row.InstrumentAudioId), &bDuplicatePair);
			TestTrue(*FString::Printf(TEXT("row %d (%s, instrument %d) is unique"), Row.Index, *Row.Name, Row.InstrumentAudioId),
				!bDuplicatePair);
			TestEqual(TEXT("FindSong returns this row"), Table.FindSong(Row.Name, Row.InstrumentAudioId), &Row);

			CheckSound(Row, TEXT("mainloop"), Row.MainLoop);
			CheckSound(Row, TEXT("intro"), Row.Intro);
			CheckSound(Row, TEXT("outro"), Row.Outro);
			for (int32 FlourishIndex = 0; FlourishIndex < 8; ++FlourishIndex)
			{
				CheckSound(Row, *FString::Printf(TEXT("flourish%d"), FlourishIndex + 1), Row.Flourish[FlourishIndex]);
			}
		}
		else if (Row.Kind == ESWGPerformanceKind::Dance)
		{
			++DanceRows;
			TestTrue(*FString::Printf(TEXT("dance row %d (%s) has no audio"), Row.Index, *Row.Name), Row.MainLoop.IsEmpty() && Row.Intro.IsEmpty() && Row.Outro.IsEmpty());
		}
	}

	AddInfo(FString::Printf(TEXT("performance.iff: %d rows (%d music, %d dance); %d distinct .snd checked, %d missing, %d missing samples"),
		Table.Rows.Num(), MusicRows, DanceRows, SoundsChecked, MissingSounds, MissingSamples));
	TestTrue(TEXT("music rows present"), MusicRows > 0);
	TestEqual(TEXT("FindByIndex(0) is null"), Table.FindByIndex(0), static_cast<const FSWGPerformanceRow*>(nullptr));
	TestEqual(TEXT("FindInstrumentAudioId(slitherhorn)"), Table.FindInstrumentAudioId(TEXT("slitherhorn")), 2);
	TestEqual(TEXT("no .snd missing"), MissingSounds, 0);
	TestEqual(TEXT("no sample missing"), MissingSamples, 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
