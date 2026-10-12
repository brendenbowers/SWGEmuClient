#include "Playback/SWGSoundInstance.h"

#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Mix/SWGAudioMix.h"
#include "Playback/SWGPcmSoundWave.h"
#include "Playback/SWGPcmStreamSource.h"
#include "SWGAudioSubsystem.h"

namespace
{
	// The mixer still holds a few buffers after the last frame is generated.
	constexpr float PlaybackTailSeconds = 0.1f;
}

void USWGSoundInstance::Initialize(USWGAudioSubsystem* InOwner, const FString& InSoundPath, TSharedPtr<const FSWGSoundTemplate> InTemplate,
	const FSWGSoundPlacement& InPlacement, const FSWGSoundPlayOptions& InOptions, int32 Seed)
{
	Owner = InOwner;
	SoundPath = InSoundPath;
	Template = MoveTemp(InTemplate);
	Placement = InPlacement;
	Options = InOptions;

	for (const FString& SamplePath : Template->Samples)
	{
		bVoice |= SamplePath.StartsWith(TEXT("voice/"));
	}

	Scheduler.Begin(*Template, Seed);
}

void USWGSoundInstance::Tick(float DeltaSeconds)
{
	if (bFinished)
	{
		return;
	}

	if (bStopping)
	{
		StopSecondsRemaining -= DeltaSeconds;
		if (StopSecondsRemaining <= 0.f || (CurrentSource.IsValid() && CurrentSource->IsExhausted()))
		{
			Finish();
		}
		return;
	}

	FSWGSoundIteration Iteration;
	if (Scheduler.Tick(DeltaSeconds, Iteration))
	{
		BeginIteration(Iteration);
		if (bFinished)
		{
			return;
		}
	}

	if (CurrentSource.IsValid())
	{
		if (!IsValid(Component) || !Component->IsPlaying())
		{
			// Stopped from outside (concurrency limit on one-shots, owner destroyed).
			Finish();
			return;
		}

		ApplyLiveParameters();
		if (CurrentSource->IsExhausted())
		{
			ExhaustedSeconds += DeltaSeconds;
			if (ExhaustedSeconds >= PlaybackTailSeconds)
			{
				EndIteration();
			}
		}
	}
	else if (!bAwaitingPcm && Scheduler.IsFinished())
	{
		Finish();
	}
}

void USWGSoundInstance::Stop(float FadeOutSeconds)
{
	if (bFinished || bStopping)
	{
		return;
	}

	Scheduler.Stop();
	++PendingRequestId;
	bAwaitingPcm = false;

	if (FadeOutSeconds > 0.f && CurrentSource.IsValid())
	{
		bStopping = true;
		CurrentSource->RequestStop(FadeOutSeconds);
		StopSecondsRemaining = FadeOutSeconds + PlaybackTailSeconds;
		return;
	}
	Finish();
}

void USWGSoundInstance::BeginIteration(const FSWGSoundIteration& Iteration)
{
	if (!Template->Samples.IsValidIndex(Iteration.SampleIndex) || !IsValid(Owner))
	{
		Finish();
		return;
	}

	bAwaitingPcm = true;
	const int32 RequestId = ++PendingRequestId;
	TWeakObjectPtr<USWGSoundInstance> WeakThis(this);
	Owner->RequestPcm(Template->Samples[Iteration.SampleIndex], [WeakThis, Iteration, RequestId](TSharedPtr<const FSWGDecodedPcm> Pcm)
	{
		if (USWGSoundInstance* Instance = WeakThis.Get())
		{
			Instance->OnPcmReady(Pcm, Iteration, RequestId);
		}
	});
}

void USWGSoundInstance::OnPcmReady(TSharedPtr<const FSWGDecodedPcm> Pcm, FSWGSoundIteration Iteration, int32 RequestId)
{
	if (bFinished || RequestId != PendingRequestId)
	{
		return;
	}

	bAwaitingPcm = false;
	if (!Pcm.IsValid() || !Pcm->IsValid())
	{
		// Missing or undecodable sample: the subsystem has already logged it once.
		Finish();
		return;
	}
	StartPlayback(Pcm, Iteration);
}

void USWGSoundInstance::StartPlayback(TSharedPtr<const FSWGDecodedPcm> Pcm, const FSWGSoundIteration& Iteration)
{
	// Only a lone infinite sample with no gap can loop inside the wave; anything else re-triggers per iteration.
	const bool bSeamlessLoop = Scheduler.IsInfinite() && Template->Samples.Num() == 1 && Template->LoopDelay.Mode == 0;

	FSWGPcmStreamSource::FConfig Config;
	Config.Pcm = Pcm;
	Config.bLoop = bSeamlessLoop;
	Config.LoopStartFrame = Options.LoopStartFrame;
	Config.LoopEndFrame = Options.LoopEndFrame;
	Config.FadeInSeconds = FMath::Max(Iteration.FadeInSeconds, Options.FadeInSeconds);
	Config.FadeOutSeconds = Iteration.FadeOutSeconds;
	CurrentSource = MakeShared<FSWGPcmStreamSource>(Config);

	USWGAudioMix* Mix = Owner->GetMix();
	CurrentWave = NewObject<USWGPcmSoundWave>(this);
	CurrentWave->SoundClassObject = Mix->GetCategoryClass(Template->CategoryValue, bVoice);
	CurrentWave->Setup(CurrentSource);
	// Indefinite duration makes the engine virtualize (keep active) a far or evicted loop; one-shots just end.
	CurrentWave->VirtualizationMode = Scheduler.IsInfinite() ? EVirtualizationMode::Restart : EVirtualizationMode::Disabled;

	UAudioComponent* AudioComponent = EnsureComponent();
	if (!AudioComponent)
	{
		Finish();
		return;
	}

	if (AudioComponent->IsPlaying())
	{
		AudioComponent->Stop();
	}
	AudioComponent->SetSound(CurrentWave);
	ApplyLiveParameters();
	ExhaustedSeconds = 0.f;
	Owner->EnsureMixApplied();
	AudioComponent->Play(0.f);
}

UAudioComponent* USWGSoundInstance::EnsureComponent()
{
	if (IsValid(Component))
	{
		return Component;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const bool b3D = (Template->b3D || Options.bForceSpatial) && Placement.Kind != FSWGSoundPlacement::EKind::Local2D;
	UAudioComponent* NewComponent = nullptr;

	if (Placement.Kind == FSWGSoundPlacement::EKind::Attached)
	{
		USceneComponent* Parent = Placement.AttachParent.Get();
		if (!IsValid(Parent) || !Parent->GetOwner())
		{
			return nullptr;
		}
		NewComponent = NewObject<UAudioComponent>(Parent->GetOwner());
		NewComponent->bStopWhenOwnerDestroyed = true;
		NewComponent->RegisterComponent();
		NewComponent->AttachToComponent(Parent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Placement.Socket);
	}
	else
	{
		NewComponent = NewObject<UAudioComponent>(GetTransientPackage());
		if (Placement.Kind == FSWGSoundPlacement::EKind::World)
		{
			NewComponent->SetWorldLocation(Placement.Location);
		}
		NewComponent->RegisterComponentWithWorld(World);
	}

	NewComponent->bAutoActivate = false;
	NewComponent->bAutoDestroy = false;
	NewComponent->bIsUISound = Template->CategoryValue == static_cast<int32>(ESWGSoundCategory::UI);
	NewComponent->bAllowSpatialization = b3D;
	if (b3D)
	{
		const float DistanceMeters = Template->b3D || Template->DistanceMeters >= 1.f ? Template->DistanceMeters : 0.f;
		NewComponent->AttenuationSettings = Owner->GetMix()->GetAttenuation(
			Template->b3D && DistanceMeters <= 0.f ? USWGAudioMix::Retail3dDefaultDistanceMeters : DistanceMeters);
	}
	if (USoundConcurrency* Concurrency = Owner->GetMix()->GetConcurrency(Template->CategoryValue, bVoice))
	{
		NewComponent->ConcurrencySet.Add(Concurrency);
	}

	Component = NewComponent;
	return Component;
}

void USWGSoundInstance::ApplyLiveParameters()
{
	if (IsValid(Component))
	{
		Component->SetVolumeMultiplier(Scheduler.GetVolume() * Options.VolumeScale);
		Component->SetPitchMultiplier(Scheduler.GetPitchMultiplier() * Options.PitchScale);
	}
}

void USWGSoundInstance::EndIteration()
{
	if (IsValid(Component))
	{
		Component->Stop();
	}
	CurrentSource.Reset();
	CurrentWave = nullptr;
	Scheduler.NotifyIterationFinished();
	if (Scheduler.IsFinished())
	{
		Finish();
	}
}

void USWGSoundInstance::Finish()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	bAwaitingPcm = false;
	++PendingRequestId;

	if (IsValid(Component))
	{
		Component->Stop();
		Component->DestroyComponent();
	}
	Component = nullptr;
	CurrentWave = nullptr;
	CurrentSource.Reset();
}

bool FSWGSoundHandle::IsPlaying() const
{
	const USWGSoundInstance* Strong = Instance.Get();
	return Strong && !Strong->IsFinished();
}

void FSWGSoundHandle::Stop(float FadeOutSeconds) const
{
	if (USWGSoundInstance* Strong = Instance.Get())
	{
		Strong->Stop(FadeOutSeconds);
	}
}
