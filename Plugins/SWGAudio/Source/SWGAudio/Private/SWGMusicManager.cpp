#include "SWGMusicManager.h"

#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "SWGAudioSubsystem.h"
#include "SWGAudioTypes.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGDataTableReader.h"

namespace
{
	struct FLoopPoints
	{
		int32 StartFrame = INDEX_NONE;
		int32 EndFrame = INDEX_NONE;
	};

	class FSubsystemMusicBackend : public ISWGMusicBackend
	{
	public:
		explicit FSubsystemMusicBackend(USWGAudioSubsystem* InAudio) : Audio(InAudio) {}

		virtual int32 Start(const FString& SndPath, float FadeInSeconds) override
		{
			USWGAudioSubsystem* AudioSubsystem = Audio.Get();
			if (!AudioSubsystem)
			{
				return 0;
			}

			FSWGSoundPlayOptions Options;
			Options.FadeInSeconds = FadeInSeconds;
			if (const TSharedPtr<const FSWGSoundTemplate> Template = AudioSubsystem->GetSoundTemplate(SWGAudio::NormalizeSoundPath(SndPath)))
			{
				if (!Template->Samples.IsEmpty())
				{
					const FLoopPoints Points = FindLoopPoints(Template->Samples[0]);
					Options.LoopStartFrame = Points.StartFrame;
					Options.LoopEndFrame = Points.EndFrame;
				}
			}

			const FSWGSoundHandle Handle = AudioSubsystem->PlaySound2D(SndPath, Options);
			if (!Handle.IsValid())
			{
				return 0;
			}
			const int32 Token = ++LastToken;
			Handles.Add(Token, Handle);
			return Token;
		}

		virtual bool IsPlaying(int32 Token) const override
		{
			const FSWGSoundHandle* Handle = Handles.Find(Token);
			return Handle && Handle->IsPlaying();
		}

		virtual void Stop(int32 Token, float FadeOutSeconds) override
		{
			FSWGSoundHandle Handle;
			if (Handles.RemoveAndCopyValue(Token, Handle))
			{
				Handle.Stop(FadeOutSeconds);
			}
		}

	private:
		/** music.iff rows are keyed by file name; the File column is a full path for most rows and bare for some. */
		FLoopPoints FindLoopPoints(const FString& SamplePath)
		{
			if (!bTableLoaded)
			{
				bTableLoaded = true;
				LoadTable();
			}
			const FLoopPoints* Found = LoopTable.Find(FPaths::GetCleanFilename(SamplePath).ToLower());
			return Found ? *Found : FLoopPoints();
		}

		void LoadTable()
		{
			USWGAudioSubsystem* AudioSubsystem = Audio.Get();
			UGameInstance* GameInstance = AudioSubsystem ? AudioSubsystem->GetGameInstance() : nullptr;
			USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
			FSWGDataTableData Table;
			if (!Tre || !FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/music/music.iff")), Table))
			{
				return;
			}
			const int32 FileColumn = Table.GetColumnIndex(TEXT("File"));
			const int32 StartColumn = Table.GetColumnIndex(TEXT("Loop Start"));
			const int32 EndColumn = Table.GetColumnIndex(TEXT("Loop End"));
			if (FileColumn == INDEX_NONE || StartColumn == INDEX_NONE || EndColumn == INDEX_NONE)
			{
				return;
			}
			for (const FSWGDataTableRow& Row : Table.Rows)
			{
				if (Row.Cells.IsValidIndex(FileColumn) && Row.Cells.IsValidIndex(StartColumn) && Row.Cells.IsValidIndex(EndColumn))
				{
					FLoopPoints Points;
					Points.StartFrame = FCString::Atoi(*Row.Cells[StartColumn]);
					Points.EndFrame = FCString::Atoi(*Row.Cells[EndColumn]);
					LoopTable.Add(FPaths::GetCleanFilename(Row.Cells[FileColumn].Replace(TEXT("\\"), TEXT("/"))).ToLower(), Points);
				}
			}
		}

		TWeakObjectPtr<USWGAudioSubsystem> Audio;
		TMap<int32, FSWGSoundHandle> Handles;
		TMap<FString, FLoopPoints> LoopTable;
		int32 LastToken = 0;
		bool bTableLoaded = false;
	};
}

void USWGMusicManager::Initialize(USWGAudioSubsystem* InAudio)
{
	Backend = MakeShared<FSubsystemMusicBackend>(InAudio);
}

void USWGMusicManager::SetBackend(TSharedPtr<ISWGMusicBackend> InBackend)
{
	Backend = MoveTemp(InBackend);
}

bool USWGMusicManager::IsSilence(const FString& SndPath)
{
	const FString Name = FPaths::GetBaseFilename(SndPath);
	return Name.IsEmpty() || Name.Equals(TEXT("none"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("music_silence"), ESearchCase::IgnoreCase);
}

ESWGMusicLayer USWGMusicManager::GetAudibleLayer() const
{
	for (int32 LayerIndex = static_cast<int32>(ESWGMusicLayer::Count) - 1; LayerIndex >= 0; --LayerIndex)
	{
		if (!Layers[LayerIndex].Path.IsEmpty())
		{
			return static_cast<ESWGMusicLayer>(LayerIndex);
		}
	}
	return ESWGMusicLayer::Count;
}

void USWGMusicManager::SetLayer(ESWGMusicLayer Layer, const FString& SndPath, float FadeSeconds)
{
	FLayer& Slot = Layers[static_cast<int32>(Layer)];
	const FString NewPath = IsSilence(SndPath) ? FString() : SndPath;
	const float Fade = FadeSeconds < 0.f ? DefaultFadeSeconds : FadeSeconds;

	if (!NewPath.Equals(Slot.Path, ESearchCase::IgnoreCase))
	{
		if (Slot.Token != 0 && Backend)
		{
			Backend->Stop(Slot.Token, Fade);
		}
		Slot.Token = 0;
		Slot.Path = NewPath;
	}
	Refresh(Fade);
}

void USWGMusicManager::Tick(float DeltaSeconds)
{
	if (!Backend)
	{
		return;
	}
	bool bEnded = false;
	for (FLayer& Slot : Layers)
	{
		if (Slot.Token != 0 && !Backend->IsPlaying(Slot.Token))
		{
			Slot.Token = 0;
			Slot.Path.Empty();
			bEnded = true;
		}
	}
	if (bEnded)
	{
		Refresh(DefaultFadeSeconds);
	}
}

void USWGMusicManager::Refresh(float FadeSeconds)
{
	if (!Backend)
	{
		return;
	}
	// A start that fails empties its layer, so re-evaluate until something plays or nothing is left.
	for (int32 Attempt = 0; Attempt < static_cast<int32>(ESWGMusicLayer::Count); ++Attempt)
	{
		const ESWGMusicLayer Audible = GetAudibleLayer();
		for (int32 LayerIndex = 0; LayerIndex < static_cast<int32>(ESWGMusicLayer::Count); ++LayerIndex)
		{
			FLayer& Slot = Layers[LayerIndex];
			if (LayerIndex != static_cast<int32>(Audible) && Slot.Token != 0)
			{
				Backend->Stop(Slot.Token, FadeSeconds);
				Slot.Token = 0;
			}
		}
		if (Audible == ESWGMusicLayer::Count)
		{
			return;
		}
		FLayer& Active = Layers[static_cast<int32>(Audible)];
		if (Active.Token != 0)
		{
			return;
		}
		Active.Token = Backend->Start(Active.Path, FadeSeconds);
		if (Active.Token != 0)
		{
			return;
		}
		Active.Path.Empty();
	}
}
