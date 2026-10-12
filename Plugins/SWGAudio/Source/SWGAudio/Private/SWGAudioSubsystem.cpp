#include "SWGAudioSubsystem.h"

#include "AudioDevice.h"
#include "Async/Async.h"
#include "Decode/SWGMp3Decoder.h"
#include "Decode/SWGWavDecoder.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Mix/SWGAudioMix.h"
#include "SWGMusicManager.h"
#include "Playback/SWGSoundInstance.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "SWGAudioSettings.h"
#include "TRE/SWGClientEffectReader.h"
#include "TRE/SWGSoundTemplateReader.h"

namespace
{
	constexpr int64 BytesPerMegabyte = 1024 * 1024;
}

void USWGAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<USWGTreSubsystem>();
	Super::Initialize(Collection);

	Tre = GetGameInstance()->GetSubsystem<USWGTreSubsystem>();

	USWGAudioSettings::Get()->LoadConfig();
	Mix = NewObject<USWGAudioMix>(this);
	Mix->Build();
	MusicManager = NewObject<USWGMusicManager>(this);
	MusicManager->Initialize(this);
	ApplySettings();

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USWGAudioSubsystem::Tick));
}

void USWGAudioSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	StopAll();
	ActiveInstances.Reset();
	PendingDecodes.Reset();
	Super::Deinitialize();
}

bool USWGAudioSubsystem::Tick(float DeltaSeconds)
{
	for (int32 InstanceIndex = ActiveInstances.Num() - 1; InstanceIndex >= 0; --InstanceIndex)
	{
		USWGSoundInstance* Instance = ActiveInstances[InstanceIndex];
		if (!IsValid(Instance))
		{
			ActiveInstances.RemoveAtSwap(InstanceIndex);
			continue;
		}
		Instance->Tick(DeltaSeconds);
		if (Instance->IsFinished())
		{
			ActiveInstances.RemoveAtSwap(InstanceIndex);
		}
	}
	if (MusicManager)
	{
		MusicManager->Tick(DeltaSeconds);
	}
	return true;
}

// ── Playback ─────────────────────────────────────────────────────────────

FSWGSoundHandle USWGAudioSubsystem::StartInstance(const FString& SoundReference, const FSWGSoundPlacement& Placement, const FSWGSoundPlayOptions& Options)
{
	FSWGSoundHandle Handle;
	const FString Path = SWGAudio::NormalizeSoundPath(SoundReference);
	const TSharedPtr<const FSWGSoundTemplate> Template = GetSoundTemplate(Path);
	if (!Template.IsValid() || Template->Samples.IsEmpty() || !GetWorld())
	{
		return Handle;
	}

	USWGSoundInstance* Instance = NewObject<USWGSoundInstance>(this);
	Instance->Initialize(this, Path, Template, Placement, Options, FMath::Rand());
	ActiveInstances.Add(Instance);
	Handle.Instance = Instance;
	return Handle;
}

FSWGSoundHandle USWGAudioSubsystem::PlaySound2D(const FString& SndPath, const FSWGSoundPlayOptions& Options)
{
	return StartInstance(SndPath, FSWGSoundPlacement(), Options);
}

FSWGSoundHandle USWGAudioSubsystem::PlaySoundAtLocation(const FString& SndPath, const FVector& WorldLocation, const FSWGSoundPlayOptions& Options)
{
	FSWGSoundPlacement Placement;
	Placement.Kind = FSWGSoundPlacement::EKind::World;
	Placement.Location = WorldLocation;
	return StartInstance(SndPath, Placement, Options);
}

FSWGSoundHandle USWGAudioSubsystem::PlaySoundAttached(const FString& SndPath, USceneComponent* Parent, FName SocketName, const FSWGSoundPlayOptions& Options)
{
	if (!Parent)
	{
		return FSWGSoundHandle();
	}
	FSWGSoundPlacement Placement;
	Placement.Kind = FSWGSoundPlacement::EKind::Attached;
	Placement.AttachParent = Parent;
	Placement.Socket = SocketName;
	return StartInstance(SndPath, Placement, Options);
}

TArray<FSWGSoundHandle> USWGAudioSubsystem::PlayClientEffectSounds(const FString& CefPath)
{
	TArray<FSWGSoundHandle> Handles;
	if (const TSharedPtr<const FSWGClientEffect> Effect = GetClientEffect(SWGAudio::NormalizeEffectPath(CefPath)))
	{
		for (const FString& SoundPath : Effect->SoundPaths)
		{
			Handles.Add(PlaySound2D(SoundPath));
		}
	}
	return Handles;
}

TArray<FSWGSoundHandle> USWGAudioSubsystem::PlayClientEffectSoundsAtLocation(const FString& CefPath, const FVector& WorldLocation)
{
	TArray<FSWGSoundHandle> Handles;
	if (const TSharedPtr<const FSWGClientEffect> Effect = GetClientEffect(SWGAudio::NormalizeEffectPath(CefPath)))
	{
		for (const FString& SoundPath : Effect->SoundPaths)
		{
			Handles.Add(PlaySoundAtLocation(SoundPath, WorldLocation));
		}
	}
	return Handles;
}

TArray<FSWGSoundHandle> USWGAudioSubsystem::PlayClientEffectSoundsAttached(const FString& CefPath, USceneComponent* Parent, FName SocketName)
{
	TArray<FSWGSoundHandle> Handles;
	if (const TSharedPtr<const FSWGClientEffect> Effect = GetClientEffect(SWGAudio::NormalizeEffectPath(CefPath)))
	{
		for (const FString& SoundPath : Effect->SoundPaths)
		{
			Handles.Add(PlaySoundAttached(SoundPath, Parent, SocketName));
		}
	}
	return Handles;
}

FSWGSoundHandle USWGAudioSubsystem::PlayMusic(const FString& SamplePath, int32 LoopStartFrame, int32 LoopEndFrame, float FadeInSeconds)
{
	if (!GetWorld())
	{
		return FSWGSoundHandle();
	}

	// Synthetic template: one sample, infinite loop, background-music category.
	TSharedRef<FSWGSoundTemplate> Template = MakeShared<FSWGSoundTemplate>();
	Template->Samples.Add(SamplePath.Replace(TEXT("\\"), TEXT("/")));
	Template->LoopCountMin = -1;
	Template->LoopCountMax = -1;
	Template->CategoryValue = static_cast<int32>(ESWGSoundCategory::BackgroundMusic);
	Template->Volume.Min = Template->Volume.Max = 1.f;
	Template->FadeIn.Mode = FadeInSeconds > 0.f ? 1 : 0;
	Template->FadeIn.Min = Template->FadeIn.Max = FadeInSeconds;

	FSWGSoundPlayOptions Options;
	Options.LoopStartFrame = LoopStartFrame;
	Options.LoopEndFrame = LoopEndFrame;

	USWGSoundInstance* Instance = NewObject<USWGSoundInstance>(this);
	Instance->Initialize(this, SamplePath, Template, FSWGSoundPlacement(), Options, FMath::Rand());
	ActiveInstances.Add(Instance);

	FSWGSoundHandle Handle;
	Handle.Instance = Instance;
	return Handle;
}

void USWGAudioSubsystem::StopAll(float FadeOutSeconds)
{
	for (USWGSoundInstance* Instance : ActiveInstances)
	{
		if (IsValid(Instance))
		{
			Instance->Stop(FadeOutSeconds);
		}
	}
}

// ── Mixing / settings ────────────────────────────────────────────────────

void USWGAudioSubsystem::EnsureMixApplied()
{
	FAudioDevice* Device = nullptr;
	if (UWorld* World = GetWorld())
	{
		Device = World->GetAudioDeviceRaw();
	}
	if (!Device && GEngine)
	{
		Device = GEngine->GetMainAudioDeviceRaw();
	}
	if (Device && Mix)
	{
		Mix->ApplyToDevice(Device);
	}
}

void USWGAudioSubsystem::ApplyMasterVolume()
{
	const USWGAudioSettings* Settings = USWGAudioSettings::Get();
	for (int32 BusIndex = 0; BusIndex < static_cast<int32>(ESWGAudioBus::Count); ++BusIndex)
	{
		const ESWGAudioBus Bus = static_cast<ESWGAudioBus>(BusIndex);
		const float Volume = Settings->GetBusVolume(Bus);
		Mix->SetBusVolume(Bus, (Bus == ESWGAudioBus::Master && Settings->bMuted) ? 0.f : Volume);
	}
	EnsureMixApplied();
}

void USWGAudioSubsystem::SetBusVolume(ESWGAudioBus Bus, float Volume)
{
	USWGAudioSettings::Get()->SetBusVolume(Bus, Volume);
	ApplyMasterVolume();
}

float USWGAudioSubsystem::GetBusVolume(ESWGAudioBus Bus) const
{
	return USWGAudioSettings::Get()->GetBusVolume(Bus);
}

void USWGAudioSubsystem::SetMuted(bool bMuted)
{
	USWGAudioSettings::Get()->bMuted = bMuted;
	ApplyMasterVolume();
}

bool USWGAudioSubsystem::IsMuted() const
{
	return USWGAudioSettings::Get()->bMuted;
}

void USWGAudioSubsystem::ApplySettings()
{
	ApplyMasterVolume();
	TrimCache();
}

void USWGAudioSubsystem::SaveSettings()
{
	USWGAudioSettings::Get()->Save();
}

// ── Template / effect caches ─────────────────────────────────────────────

float USWGAudioSubsystem::GetAudibleRadius(const FString& SndPath, bool bForceSpatial)
{
	const TSharedPtr<const FSWGSoundTemplate> Template = GetSoundTemplate(SWGAudio::NormalizeSoundPath(SndPath));
	if (!Template.IsValid())
	{
		return 0.f;
	}
	if (!Template->b3D && !bForceSpatial)
	{
		return -1.f;
	}

	const float FallbackMeters = Template->b3D ? USWGAudioMix::Retail3dDefaultDistanceMeters : USWGAudioMix::DefaultDistanceMeters;
	const float FullVolumeMeters = Template->DistanceMeters >= 1.f || (Template->b3D && Template->DistanceMeters > 0.f) ? Template->DistanceMeters : FallbackMeters;
	return FullVolumeMeters * 100.f * USWGAudioMix::AttenuationFalloffFactor;
}

TSharedPtr<const FSWGSoundTemplate> USWGAudioSubsystem::GetSoundTemplate(const FString& NormalizedPath)
{
	if (const TSharedPtr<const FSWGSoundTemplate>* Cached = TemplateCache.Find(NormalizedPath))
	{
		return *Cached;
	}

	TSharedPtr<const FSWGSoundTemplate> Result;
	if (Tre && Tre->FileExists(NormalizedPath))
	{
		TSharedRef<FSWGSoundTemplate> Template = MakeShared<FSWGSoundTemplate>();
		if (FSWGSoundTemplateReader::Read(Tre->CreateIffReader(NormalizedPath), *Template))
		{
			Result = Template;
		}
		else
		{
			UE_LOG(LogSWGAudio, Warning, TEXT("Could not parse sound template %s"), *NormalizedPath);
		}
	}
	else
	{
		UE_LOG(LogSWGAudio, Warning, TEXT("Sound template %s not found"), *NormalizedPath);
	}

	// A null entry records the failure so it is logged once.
	TemplateCache.Add(NormalizedPath, Result);
	return Result;
}

TSharedPtr<const FSWGClientEffect> USWGAudioSubsystem::GetClientEffect(const FString& NormalizedPath)
{
	if (const TSharedPtr<const FSWGClientEffect>* Cached = EffectCache.Find(NormalizedPath))
	{
		return *Cached;
	}

	TSharedPtr<const FSWGClientEffect> Result;
	if (Tre && Tre->FileExists(NormalizedPath))
	{
		TSharedRef<FSWGClientEffect> Effect = MakeShared<FSWGClientEffect>();
		if (FSWGClientEffectReader::Read(Tre->CreateIffReader(NormalizedPath), *Effect))
		{
			Result = Effect;
		}
		else
		{
			UE_LOG(LogSWGAudio, Log, TEXT("Client effect %s is empty or unreadable"), *NormalizedPath);
		}
	}
	else
	{
		UE_LOG(LogSWGAudio, Warning, TEXT("Client effect %s not found"), *NormalizedPath);
	}

	EffectCache.Add(NormalizedPath, Result);
	return Result;
}

// ── Decoded PCM cache ────────────────────────────────────────────────────

void USWGAudioSubsystem::RequestPcm(const FString& SamplePath, FPcmCallback Callback)
{
	if (FailedSamples.Contains(SamplePath))
	{
		Callback(nullptr);
		return;
	}

	if (FPcmCacheEntry* Entry = PcmCache.Find(SamplePath))
	{
		++CacheHits;
		Entry->LastUsed = ++UseCounter;
		Callback(Entry->Pcm);
		return;
	}

	if (TArray<FPcmCallback>* Waiters = PendingDecodes.Find(SamplePath))
	{
		Waiters->Add(MoveTemp(Callback));
		return;
	}

	++CacheMisses;
	TArray<uint8> Bytes = Tre ? Tre->ExtractFile(SamplePath) : TArray<uint8>();
	if (Bytes.IsEmpty())
	{
		UE_LOG(LogSWGAudio, Warning, TEXT("Sample %s is missing from the TRE"), *SamplePath);
		FailedSamples.Add(SamplePath);
		Callback(nullptr);
		return;
	}

	PendingDecodes.Add(SamplePath).Add(MoveTemp(Callback));

	// Decode on a worker; the result hops back to the game thread before touching any UObject.
	const bool bIsMp3 = SamplePath.EndsWith(TEXT(".mp3"));
	TWeakObjectPtr<USWGAudioSubsystem> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, SamplePath, Bytes = MoveTemp(Bytes), bIsMp3]()
	{
		TSharedPtr<FSWGDecodedPcm> Pcm = MakeShared<FSWGDecodedPcm>();
		FString Error;
		const bool bOk = bIsMp3 ? FSWGMp3Decoder::Decode(Bytes, *Pcm, &Error) : FSWGWavDecoder::Decode(Bytes, *Pcm, &Error);
		if (!bOk)
		{
			UE_LOG(LogSWGAudio, Warning, TEXT("Could not decode %s: %s"), *SamplePath, *Error);
			Pcm.Reset();
		}

		AsyncTask(ENamedThreads::GameThread, [WeakThis, SamplePath, Pcm]()
		{
			if (USWGAudioSubsystem* Subsystem = WeakThis.Get())
			{
				Subsystem->OnDecodeFinished(SamplePath, Pcm);
			}
		});
	});
}

void USWGAudioSubsystem::OnDecodeFinished(const FString& SamplePath, TSharedPtr<FSWGDecodedPcm> Pcm)
{
	TArray<FPcmCallback> Waiters;
	PendingDecodes.RemoveAndCopyValue(SamplePath, Waiters);

	if (Pcm.IsValid())
	{
		InsertIntoCache(SamplePath, Pcm);
	}
	else
	{
		FailedSamples.Add(SamplePath);
	}

	for (const FPcmCallback& Waiter : Waiters)
	{
		Waiter(Pcm);
	}
}

void USWGAudioSubsystem::InsertIntoCache(const FString& SamplePath, TSharedPtr<const FSWGDecodedPcm> Pcm)
{
	FPcmCacheEntry& Entry = PcmCache.FindOrAdd(SamplePath);
	CacheBytes -= Entry.Bytes;
	Entry.Pcm = Pcm;
	Entry.Bytes = static_cast<int64>(Pcm->Samples.Num()) * sizeof(int16);
	Entry.LastUsed = ++UseCounter;
	CacheBytes += Entry.Bytes;
	TrimCache();
}

void USWGAudioSubsystem::TrimCache()
{
	const int64 Budget = static_cast<int64>(FMath::Max(1, USWGAudioSettings::Get()->PcmCacheBudgetMB)) * BytesPerMegabyte;
	while (CacheBytes > Budget && PcmCache.Num() > 1)
	{
		const FString* OldestKey = nullptr;
		uint64 OldestUse = MAX_uint64;
		for (const TPair<FString, FPcmCacheEntry>& Pair : PcmCache)
		{
			if (Pair.Value.LastUsed < OldestUse)
			{
				OldestUse = Pair.Value.LastUsed;
				OldestKey = &Pair.Key;
			}
		}
		if (!OldestKey)
		{
			break;
		}
		CacheBytes -= PcmCache[*OldestKey].Bytes;
		PcmCache.Remove(*OldestKey);
	}
}

FSWGAudioStats USWGAudioSubsystem::GetStats() const
{
	FSWGAudioStats Stats;
	for (const USWGSoundInstance* Instance : ActiveInstances)
	{
		if (IsValid(Instance) && !Instance->IsFinished())
		{
			++Stats.ActiveInstances;
			Stats.ActiveByCategory.FindOrAdd(Instance->GetCategoryValue())++;
		}
	}
	Stats.CachedSamples = PcmCache.Num();
	Stats.PendingDecodes = PendingDecodes.Num();
	Stats.CacheBytes = CacheBytes;
	Stats.BudgetBytes = static_cast<int64>(USWGAudioSettings::Get()->PcmCacheBudgetMB) * BytesPerMegabyte;
	Stats.CacheHits = CacheHits;
	Stats.CacheMisses = CacheMisses;
	Stats.CachedTemplates = TemplateCache.Num();
	return Stats;
}
