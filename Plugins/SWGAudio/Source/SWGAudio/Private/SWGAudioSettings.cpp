#include "SWGAudioSettings.h"

USWGAudioSettings* USWGAudioSettings::Get()
{
	return GetMutableDefault<USWGAudioSettings>();
}

void USWGAudioSettings::ResetToDefaults()
{
	for (int32 BusIndex = 0; BusIndex < static_cast<int32>(ESWGAudioBus::Count); ++BusIndex)
	{
		const ESWGAudioBus Bus = static_cast<ESWGAudioBus>(BusIndex);
		SetBusVolume(Bus, Bus == ESWGAudioBus::Music ? DefaultMusicVolume : 1.f);
	}
	bMuted = false;
}

float USWGAudioSettings::GetBusVolume(ESWGAudioBus Bus) const
{
	switch (Bus)
	{
	case ESWGAudioBus::Master: return MasterVolume;
	case ESWGAudioBus::Music: return MusicVolume;
	case ESWGAudioBus::PlayerMusic: return PlayerMusicVolume;
	case ESWGAudioBus::Ambient: return AmbientVolume;
	case ESWGAudioBus::Effects: return EffectsVolume;
	case ESWGAudioBus::Combat: return CombatVolume;
	case ESWGAudioBus::Creature: return CreatureVolume;
	case ESWGAudioBus::UI: return UIVolume;
	default: return 1.f;
	}
}

void USWGAudioSettings::SetBusVolume(ESWGAudioBus Bus, float Volume)
{
	Volume = FMath::Clamp(Volume, 0.f, 1.f);
	switch (Bus)
	{
	case ESWGAudioBus::Master: MasterVolume = Volume; break;
	case ESWGAudioBus::Music: MusicVolume = Volume; break;
	case ESWGAudioBus::PlayerMusic: PlayerMusicVolume = Volume; break;
	case ESWGAudioBus::Ambient: AmbientVolume = Volume; break;
	case ESWGAudioBus::Effects: EffectsVolume = Volume; break;
	case ESWGAudioBus::Combat: CombatVolume = Volume; break;
	case ESWGAudioBus::Creature: CreatureVolume = Volume; break;
	case ESWGAudioBus::UI: UIVolume = Volume; break;
	default: break;
	}
}
