#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Decode/SWGDecodedPcm.h"
#include "Playback/SWGSoundHandle.h"
#include "SWGAudioTypes.h"
#include "SWGAudioSubsystem.generated.h"

struct FSWGClientEffect;
struct FSWGSoundPlacement;
class USceneComponent;
class USWGAudioMix;
class USWGMusicManager;
class USWGSoundInstance;
class USWGTreSubsystem;

/** Snapshot for swg.Audio.Stats. */
struct FSWGAudioStats
{
	TMap<int32, int32> ActiveByCategory;
	int32 ActiveInstances = 0;
	int32 CachedSamples = 0;
	int32 PendingDecodes = 0;
	int64 CacheBytes = 0;
	int64 BudgetBytes = 0;
	int32 CacheHits = 0;
	int32 CacheMisses = 0;
	int32 CachedTemplates = 0;
};

/**
 * Entry point for all sound playback. Resolves .snd/.cef from the TRE, decodes samples
 * off-thread into a budgeted LRU, and runs one USWGSoundInstance per played sound.
 *
 * Paths: "ui_rollover", "sound/ui_rollover" and "sound/ui_rollover.snd" are equivalent
 * (see SWGAudio::NormalizeSoundPath); a reference with any other directory is used as given.
 */
UCLASS()
class SWGAUDIO_API USWGAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── Playback ─────────────────────────────────────────────────────────

	/** Non-spatialised, regardless of whether the .snd is a 3D one. */
	FSWGSoundHandle PlaySound2D(const FString& SndPath, const FSWGSoundPlayOptions& Options = FSWGSoundPlayOptions());

	/** Spatialised at a fixed point when the .snd is 3D (t27 distance), otherwise 2D. */
	FSWGSoundHandle PlaySoundAtLocation(const FString& SndPath, const FVector& WorldLocation, const FSWGSoundPlayOptions& Options = FSWGSoundPlayOptions());

	/** Follows Parent (optionally a socket); ends if the component goes away. */
	FSWGSoundHandle PlaySoundAttached(const FString& SndPath, USceneComponent* Parent, FName SocketName = NAME_None, const FSWGSoundPlayOptions& Options = FSWGSoundPlayOptions());

	/** Plays every PSND of a .cef layered, one handle each. Particles/lights/shake are ignored here. */
	TArray<FSWGSoundHandle> PlayClientEffectSounds(const FString& CefPath);
	TArray<FSWGSoundHandle> PlayClientEffectSoundsAtLocation(const FString& CefPath, const FVector& WorldLocation);
	TArray<FSWGSoundHandle> PlayClientEffectSoundsAttached(const FString& CefPath, USceneComponent* Parent, FName SocketName = NAME_None);

	/**
	 * Plays a music file (usually .mp3) as an infinite 2D loop on the music bus. Loop points are PCM
	 * frames; pass INDEX_NONE for the file's own (or none, looping the whole file).
	 */
	FSWGSoundHandle PlayMusic(const FString& SamplePath, int32 LoopStartFrame = INDEX_NONE, int32 LoopEndFrame = INDEX_NONE, float FadeInSeconds = 0.f);

	void StopAll(float FadeOutSeconds = 0.f);

	// ── Mixing / settings ────────────────────────────────────────────────

	void SetBusVolume(ESWGAudioBus Bus, float Volume);
	float GetBusVolume(ESWGAudioBus Bus) const;
	void SetMuted(bool bMuted);
	bool IsMuted() const;
	/** Re-reads USWGAudioSettings (volumes, mute, cache budget) and applies them. */
	void ApplySettings();
	/** Persists the current settings. */
	void SaveSettings();

	USWGAudioMix* GetMix() const { return Mix; }
	/** Priority-layered music channel; see USWGMusicManager. */
	USWGMusicManager* GetMusicManager() const { return MusicManager; }

	// ── Caches / diagnostics ─────────────────────────────────────────────

	TSharedPtr<const FSWGSoundTemplate> GetSoundTemplate(const FString& NormalizedPath);
	TSharedPtr<const FSWGClientEffect> GetClientEffect(const FString& NormalizedPath);

	using FPcmCallback = TFunction<void(TSharedPtr<const FSWGDecodedPcm>)>;
	/** Calls Callback on the game thread, with null if the sample is missing or fails to decode. */
	void RequestPcm(const FString& SamplePath, FPcmCallback Callback);

	/**
	 * Distance in cm past which a spatialised .snd is silent (full-volume distance x falloff factor).
	 * Negative for a 2D sound (audible anywhere), 0 when the .snd is missing; lets callers skip
	 * inaudible one-shots before any PCM is decoded.
	 */
	float GetAudibleRadius(const FString& SndPath, bool bForceSpatial = false);

	/** Makes sure the mix is active on the current world's audio device. */
	void EnsureMixApplied();

	FSWGAudioStats GetStats() const;

private:
	FSWGSoundHandle StartInstance(const FString& SoundReference, const FSWGSoundPlacement& Placement, const FSWGSoundPlayOptions& Options);
	void OnDecodeFinished(const FString& SamplePath, TSharedPtr<FSWGDecodedPcm> Pcm);
	void InsertIntoCache(const FString& SamplePath, TSharedPtr<const FSWGDecodedPcm> Pcm);
	void TrimCache();
	void ApplyMasterVolume();
	bool Tick(float DeltaSeconds);

	UPROPERTY()
	TObjectPtr<USWGAudioMix> Mix;

	UPROPERTY()
	TObjectPtr<USWGMusicManager> MusicManager;

	UPROPERTY()
	TArray<TObjectPtr<USWGSoundInstance>> ActiveInstances;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	TMap<FString, TSharedPtr<const FSWGSoundTemplate>> TemplateCache;
	TMap<FString, TSharedPtr<const FSWGClientEffect>> EffectCache;

	struct FPcmCacheEntry
	{
		TSharedPtr<const FSWGDecodedPcm> Pcm;
		uint64 LastUsed = 0;
		int64 Bytes = 0;
	};
	TMap<FString, FPcmCacheEntry> PcmCache;
	TMap<FString, TArray<FPcmCallback>> PendingDecodes;
	TSet<FString> FailedSamples;
	uint64 UseCounter = 0;
	int64 CacheBytes = 0;
	int32 CacheHits = 0;
	int32 CacheMisses = 0;

	FTSTicker::FDelegateHandle TickHandle;
};
