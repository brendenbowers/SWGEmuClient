#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SWGMusicManager.generated.h"

class USWGAudioSubsystem;

/** Music sources in priority order: a higher value masks every lower one. */
UENUM()
enum class ESWGMusicLayer : uint8
{
	Environment,
	Combat,
	Server,
	Count UMETA(Hidden),
};

/** What the manager drives; the default one plays through USWGAudioSubsystem, tests substitute a fake. */
class SWGAUDIO_API ISWGMusicBackend
{
public:
	virtual ~ISWGMusicBackend() = default;

	/** Starts a .snd and returns a token, or 0 if it could not be started. */
	virtual int32 Start(const FString& SndPath, float FadeInSeconds) = 0;
	virtual bool IsPlaying(int32 Token) const = 0;
	virtual void Stop(int32 Token, float FadeOutSeconds) = 0;
};

/**
 * One music channel fed by priority layers. Only the highest layer with a sound is audible;
 * lower layers are stopped and restart from the top when they become audible again.
 * A sound that ends by itself (a sting) empties its layer, which hands the channel back down.
 * Loop-forever templates stay until replaced or cleared.
 */
UCLASS()
class SWGAUDIO_API USWGMusicManager : public UObject
{
	GENERATED_BODY()

public:
	/** Crossfade length when a caller does not choose one. */
	static constexpr float DefaultFadeSeconds = 2.f;

	/** Plays through the audio subsystem, applying music.iff loop points. */
	void Initialize(USWGAudioSubsystem* InAudio);
	void SetBackend(TSharedPtr<ISWGMusicBackend> InBackend);

	/**
	 * Sets what a layer wants to play. An empty path, "none" or music_silence.snd clears it.
	 * FadeSeconds < 0 uses DefaultFadeSeconds.
	 */
	void SetLayer(ESWGMusicLayer Layer, const FString& SndPath, float FadeSeconds = -1.f);

	void Tick(float DeltaSeconds);

	/** True for the empty path, "none" and music_silence. */
	static bool IsSilence(const FString& SndPath);

	const FString& GetLayerPath(ESWGMusicLayer Layer) const { return Layers[static_cast<int32>(Layer)].Path; }
	/** The layer that is audible, or Count if none. */
	ESWGMusicLayer GetAudibleLayer() const;

private:
	struct FLayer
	{
		FString Path;
		int32 Token = 0;
	};

	void Refresh(float FadeSeconds);

	FLayer Layers[static_cast<int32>(ESWGMusicLayer::Count)];
	TSharedPtr<ISWGMusicBackend> Backend;
};
