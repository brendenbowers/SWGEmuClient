#include "SWGAudioTypes.h"

namespace SWGAudio
{
	const TCHAR* GetBusName(ESWGAudioBus Bus)
	{
		switch (Bus)
		{
		case ESWGAudioBus::Master: return TEXT("Master");
		case ESWGAudioBus::Music: return TEXT("Music");
		case ESWGAudioBus::PlayerMusic: return TEXT("PlayerMusic");
		case ESWGAudioBus::Ambient: return TEXT("Ambient");
		case ESWGAudioBus::Effects: return TEXT("Effects");
		case ESWGAudioBus::Combat: return TEXT("Combat");
		case ESWGAudioBus::Creature: return TEXT("Creature");
		case ESWGAudioBus::UI: return TEXT("UI");
		default: return TEXT("Unknown");
		}
	}

	ESWGAudioBus GetBusForCategory(int32 CategoryValue, bool bVoice)
	{
		if (bVoice)
		{
			return ESWGAudioBus::Creature;
		}

		switch (static_cast<ESWGSoundCategory>(CategoryValue))
		{
		case ESWGSoundCategory::Ambient: return ESWGAudioBus::Ambient;
		case ESWGSoundCategory::UI: return ESWGAudioBus::UI;
		case ESWGSoundCategory::Creature: return ESWGAudioBus::Creature;
		case ESWGSoundCategory::Weapon: return ESWGAudioBus::Combat;
		case ESWGSoundCategory::BackgroundMusic:
		case ESWGSoundCategory::Special: return ESWGAudioBus::Music;
		case ESWGSoundCategory::PlayerMusic: return ESWGAudioBus::PlayerMusic;
		default: return ESWGAudioBus::Effects;
		}
	}

	namespace
	{
		FString NormalizeReference(const FString& Reference, const TCHAR* Extension, const TCHAR* DefaultDirectory)
		{
			FString Path = Reference.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/")).ToLower();
			if (Path.IsEmpty())
			{
				return Path;
			}
			if (!Path.EndsWith(Extension))
			{
				Path += Extension;
			}
			if (!Path.Contains(TEXT("/")))
			{
				Path = FString(DefaultDirectory) + Path;
			}
			return Path;
		}
	}

	FString NormalizeSoundPath(const FString& Reference)
	{
		return NormalizeReference(Reference, TEXT(".snd"), TEXT("sound/"));
	}

	FString NormalizeEffectPath(const FString& Reference)
	{
		return NormalizeReference(Reference, TEXT(".cef"), TEXT("clienteffect/"));
	}
}
