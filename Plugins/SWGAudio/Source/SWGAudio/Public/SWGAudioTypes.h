#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGSoundTemplateReader.h"
#include "SWGAudioTypes.generated.h"

SWGAUDIO_API DECLARE_LOG_CATEGORY_EXTERN(LogSWGAudio, Log, All);

/** Optional per-play tweaks. Loop frames (PCM frames) override a sample's own loop points, e.g. from music.iff. */
struct FSWGSoundPlayOptions
{
	float VolumeScale = 1.f;
	float PitchScale = 1.f;
	int32 LoopStartFrame = INDEX_NONE;
	int32 LoopEndFrame = INDEX_NONE;
	/** Spatialise even a 2D-kind .snd (sound objects); a distance under 1 m falls back to the default. */
	bool bForceSpatial = false;
	/** Extra fade-in on each iteration; the template's own fade-in applies if longer. */
	float FadeInSeconds = 0.f;
};

/** User-facing volume buses; each owns the retail sound categories listed in ESWGAudioBus mapping (see USWGAudioMix). */
UENUM(BlueprintType)
enum class ESWGAudioBus : uint8
{
	Master,
	/** Background music (8) and the single special stinger (12). */
	Music,
	/** Entertainer / player-performed music (9). */
	PlayerMusic,
	/** Ambient beds (0). */
	Ambient,
	/** Explosion (1), Item (2), Movement (3), Vehicle (5), Machine (10), and unknown categories. */
	Effects,
	/** Weapons (7). */
	Combat,
	/** Creature (6) plus every sample under voice/. */
	Creature,
	/** UI (4). */
	UI,
	Count UMETA(Hidden),
};

namespace SWGAudio
{
	SWGAUDIO_API const TCHAR* GetBusName(ESWGAudioBus Bus);

	/** Bus a retail category value belongs to; bVoice routes voice/ samples to Creature. */
	SWGAUDIO_API ESWGAudioBus GetBusForCategory(int32 CategoryValue, bool bVoice = false);

	/**
	 * Normalises a sound reference to a TRE path. Backslashes become '/', ".snd" is appended when
	 * missing, and a bare name with no directory ("ui_rollover") gets the "sound/" prefix.
	 * Anything that already has a directory is left alone, so "music/x" is never guessed at.
	 */
	SWGAUDIO_API FString NormalizeSoundPath(const FString& Reference);

	/** Same rule for clienteffects: ".cef" appended, bare names get "clienteffect/". */
	SWGAUDIO_API FString NormalizeEffectPath(const FString& Reference);
}
