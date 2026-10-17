#include "SWGUISound.h"

#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SWGAudioSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGUIStyleReader.h"
#include "UObject/GCObject.h"

namespace
{
	// The button style retail's HUD buttons use; the other button styles name the same sounds.
	const TCHAR* DefaultButtonStyle = TEXT("new.buttons.hud.style");
	const TCHAR* ChatTextboxStyle = TEXT("textbox.chat.style_bold");

	/** Retail writes bare names ("ui_deselect"); SWGAudio resolves those to sound/<name>.snd. */
	FString StyleSoundOrDefault(const FSWGUIStyleSheet* Sheet, const TCHAR* Element, const TCHAR* Attribute, const TCHAR* Fallback)
	{
		const FString* Found = Sheet ? Sheet->FindElementSound(Element, Attribute) : nullptr;
		return Found ? *Found : FString(Fallback);
	}

	USWGAudioSubsystem* GetAudio(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGAudioSubsystem>() : nullptr;
	}

	/** Nothing else references a button's sound helper, so without this the GC would reap it and the sounds would stop. */
	class FButtonSoundKeeper final : public FGCObject
	{
	public:
		void Add(UButton* Button, USWGUIButtonSound* Sound)
		{
			for (auto It = Sounds.CreateIterator(); It; ++It)
			{
				if (!It.Key().IsValid())
				{
					It.RemoveCurrent();
				}
			}
			Sounds.Add(Button, Sound);
		}

		bool Has(UButton* Button) const { return Sounds.Contains(Button); }

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override 
		{
			for (TPair<TWeakObjectPtr<UButton>, TObjectPtr<USWGUIButtonSound>>& Entry : Sounds)
			{
				Collector.AddReferencedObject(Entry.Value);
			}
		}

		virtual FString GetReferencerName() const override { return TEXT("SWGUISound"); }

	private:
		TMap<TWeakObjectPtr<UButton>, TObjectPtr<USWGUIButtonSound>> Sounds;
	};

	void Play(const UObject* WorldContext, const FString& SoundPath)
	{
		if (SoundPath.IsEmpty())
		{
			return;
		}
		if (USWGAudioSubsystem* Audio = GetAudio(WorldContext))
		{
			Audio->PlaySound2D(SoundPath);
		}
	}
}

FSWGUISoundNames FSWGUISoundNames::Resolve(USWGTreSubsystem* Tre)
{
	FSWGUISoundNames Names;
	const FSWGUIStyleSheet* Sheet = Tre ? Tre->GetUIStyleSheet() : nullptr;
	Names.Hover = StyleSoundOrDefault(Sheet, DefaultButtonStyle, TEXT("MouseOverSound"), TEXT("ui_rollover"));
	Names.Click = StyleSoundOrDefault(Sheet, DefaultButtonStyle, TEXT("ReleaseSound"), TEXT("ui_deselect"));
	Names.Keyclick = StyleSoundOrDefault(Sheet, ChatTextboxStyle, TEXT("SoundKeyclick"), TEXT("ui_keyboard_clicking"));

	static const FString TablePath = TEXT("datatables/player/sounds.iff");
	FSWGDataTableData Table;
	if (Tre && Tre->FileExists(TablePath) && FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TablePath), Table))
	{
		const int32 NameColumn = Table.GetColumnIndex(TEXT("name"));
		const int32 PathColumn = Table.GetColumnIndex(TEXT("path"));
		if (NameColumn != INDEX_NONE && PathColumn != INDEX_NONE)
		{
			for (const FSWGDataTableRow& Row : Table.Rows)
			{
				Names.Named.Add(Row.Cells[NameColumn].ToLower(), Row.Cells[PathColumn]);
			}
		}
	}
	return Names;
}

namespace SWGUISound
{
	const FSWGUISoundNames& GetNames(const UObject* WorldContext)
	{
		static TWeakObjectPtr<USWGTreSubsystem> CachedFor;
		static FSWGUISoundNames Cached;

		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
		if (!CachedFor.IsValid() || CachedFor.Get() != Tre)
		{
			Cached = FSWGUISoundNames::Resolve(Tre);
			CachedFor = Tre;
		}
		return Cached;
	}

	void PlayHover(const UObject* WorldContext) { Play(WorldContext, GetNames(WorldContext).Hover); }
	void PlayClick(const UObject* WorldContext) { Play(WorldContext, GetNames(WorldContext).Click); }
	void PlayKeyclick(const UObject* WorldContext) { Play(WorldContext, GetNames(WorldContext).Keyclick); }

	void PlayNamed(const UObject* WorldContext, const FString& Name)
	{
		const FString* Path = GetNames(WorldContext).Named.Find(Name.ToLower());
		Play(WorldContext, Path ? *Path : Name);
	}

	void BindButton(UButton* Button)
	{
		if (!Button)
		{
			return;
		}
		// A widget that rebinds on every construct must not stack sounds.
		static FButtonSoundKeeper Keeper;
		if (Keeper.Has(Button))
		{
			return;
		}
		// Not outered to the button: the keeper would then pin the button itself.
		USWGUIButtonSound* Sound = NewObject<USWGUIButtonSound>(GetTransientPackage());
		Sound->Button = Button;
		Keeper.Add(Button, Sound);
		Button->OnHovered.AddDynamic(Sound, &USWGUIButtonSound::HandleHovered);
		Button->OnClicked.AddDynamic(Sound, &USWGUIButtonSound::HandleClicked);
	}
}

void USWGUIButtonSound::HandleHovered()
{
	if (Button.IsValid() && Button->GetIsEnabled())
	{
		SWGUISound::PlayHover(Button.Get());
	}
}

void USWGUIButtonSound::HandleClicked()
{
	SWGUISound::PlayClick(Button.Get());
}
