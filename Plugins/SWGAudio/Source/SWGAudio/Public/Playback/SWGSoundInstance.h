#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Decode/SWGDecodedPcm.h"
#include "Playback/SWGSoundScheduler.h"
#include "SWGAudioTypes.h"
#include "SWGSoundInstance.generated.h"

class FSWGPcmStreamSource;
class UAudioComponent;
class USceneComponent;
class USWGAudioSubsystem;
class USWGPcmSoundWave;

/** Where a sound plays: screen-space, a fixed world point, or following a component. */
struct FSWGSoundPlacement
{
	enum class EKind : uint8 { Local2D, World, Attached };

	EKind Kind = EKind::Local2D;
	FVector Location = FVector::ZeroVector;
	TWeakObjectPtr<USceneComponent> AttachParent;
	FName Socket = NAME_None;
};

/**
 * One playing .snd. Drives an FSWGSoundScheduler and turns each iteration into a
 * procedural wave on a UAudioComponent (a new wave per iteration, since samples in
 * one template may differ in rate or channel count).
 */
UCLASS()
class SWGAUDIO_API USWGSoundInstance : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(USWGAudioSubsystem* InOwner, const FString& InSoundPath, TSharedPtr<const FSWGSoundTemplate> InTemplate,
		const FSWGSoundPlacement& InPlacement, const FSWGSoundPlayOptions& InOptions, int32 Seed);

	void Tick(float DeltaSeconds);

	/** Fades out over FadeOutSeconds (0 = at once) and finishes. */
	void Stop(float FadeOutSeconds);

	bool IsFinished() const { return bFinished; }
	int32 GetCategoryValue() const { return Template ? Template->CategoryValue : 0; }
	const FString& GetSoundPath() const { return SoundPath; }

private:
	void BeginIteration(const FSWGSoundIteration& Iteration);
	void OnPcmReady(TSharedPtr<const FSWGDecodedPcm> Pcm, FSWGSoundIteration Iteration, int32 RequestId);
	void StartPlayback(TSharedPtr<const FSWGDecodedPcm> Pcm, const FSWGSoundIteration& Iteration);
	void EndIteration();
	void Finish();
	UAudioComponent* EnsureComponent();
	void ApplyLiveParameters();

	UPROPERTY()
	TObjectPtr<USWGAudioSubsystem> Owner;

	UPROPERTY()
	TObjectPtr<UAudioComponent> Component;

	UPROPERTY()
	TObjectPtr<USWGPcmSoundWave> CurrentWave;

	FString SoundPath;
	TSharedPtr<const FSWGSoundTemplate> Template;
	FSWGSoundPlacement Placement;
	FSWGSoundPlayOptions Options;
	FSWGSoundScheduler Scheduler;
	TSharedPtr<FSWGPcmStreamSource> CurrentSource;

	int32 PendingRequestId = 0;
	bool bAwaitingPcm = false;
	bool bStopping = false;
	bool bFinished = false;
	bool bVoice = false;
	float ExhaustedSeconds = 0.f;
	float StopSecondsRemaining = 0.f;
};
