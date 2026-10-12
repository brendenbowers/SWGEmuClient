#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SWGAudioTypes.h"
#include "SWGAudioSettings.generated.h"

/** Persisted audio options (GameUserSettings.ini). The options window edits this and calls SaveConfig. */
UCLASS(Config = GameUserSettings)
class SWGAUDIO_API USWGAudioSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Every bus defaults to full volume except music. */
	static constexpr float DefaultMusicVolume = 0.8f;

	/** Per-bus volume 0..1; Master scales everything. */
	UPROPERTY(Config) float MasterVolume = 1.f;
	UPROPERTY(Config) float MusicVolume = DefaultMusicVolume;
	UPROPERTY(Config) float PlayerMusicVolume = 1.f;
	UPROPERTY(Config) float AmbientVolume = 1.f;
	UPROPERTY(Config) float EffectsVolume = 1.f;
	UPROPERTY(Config) float CombatVolume = 1.f;
	UPROPERTY(Config) float CreatureVolume = 1.f;
	UPROPERTY(Config) float UIVolume = 1.f;

	UPROPERTY(Config) bool bMuted = false;

	/** Budget for decoded PCM kept in memory, in megabytes. */
	UPROPERTY(Config) int32 PcmCacheBudgetMB = 128;

	static USWGAudioSettings* Get();

	float GetBusVolume(ESWGAudioBus Bus) const;
	void SetBusVolume(ESWGAudioBus Bus, float Volume);

	/** Volumes and mute back to their defaults; the cache budget is left alone. */
	void ResetToDefaults();

	/** Persists to GameUserSettings.ini. */
	void Save() { SaveConfig(); }
};
