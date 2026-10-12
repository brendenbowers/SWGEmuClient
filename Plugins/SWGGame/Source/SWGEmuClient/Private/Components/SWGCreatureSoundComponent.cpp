#include "Components/SWGCreatureSoundComponent.h"

#include "Audio/SWGInteriorTable.h"
#include "Common/SWGWeaponHardpoints.h"
#include "Components/SWGEquipmentComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Character.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Objects/Creature/SWGCreature.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "SWGAudioSubsystem.h"
#include "SWGAudioTypes.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGEffectSubsystem.h"
#include "TRE/SWGClientEffectReader.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGIFFChunkReader.h"
#include "TimerManager.h"

namespace
{
	// Past this nothing a creature emits reaches the listener (explosions fade out near 560 m, creatures far sooner).
	constexpr float MaxEventDistance = 20000.f;

	// The same event is never re-fired inside this window (blend-space handover, two clips sharing a step).
	constexpr double MinRepeatSeconds = 0.1;

	constexpr float LoopCheckSeconds = 0.5f;

	// An armed swing is stale after this; the fallback sounds it if its clip sent no fire/swing/hit message by then.
	constexpr float AttackWindowSeconds = 3.f;
	constexpr float AttackFallbackSeconds = 0.75f;

	// Stop an out-of-range loop only after it has drifted this far past its radius, so it doesn't flap at the edge.
	constexpr float LoopStopMargin = 1.25f;

	/** client_event_source_dest_map.iff: one EMAP is [source\0][dest\0][clienteffect\0]. */
	struct FEventMapEntry
	{
		FString Source;
		FString Dest;
		FString Target;
	};

	const TArray<FEventMapEntry>& GetEventMap(USWGTreSubsystem& Tre)
	{
		static TArray<FEventMapEntry> Entries;
		static bool bLoaded = false;
		if (bLoaded)
		{
			return Entries;
		}
		bLoaded = true;

		const FSWGIffReader Reader = Tre.CreateIffReader(TEXT("misc/client_event_source_dest_map.iff"));
		FSWGIffChunk MapForm;
		if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('E','S','D','M'), MapForm))
		{
			return Entries;
		}
		for (const FSWGIffChunk& VersionForm : Reader.FindChildForms(MapForm))
		{
			for (const FSWGIffChunk& Mapping : Reader.FindAllChildChunks(VersionForm, SWG_IFF_TAG('E','M','A','P')))
			{
				FSWGIFFChunkReader MappingReader(Mapping, Reader);
				const TArray<FString> Strings = MappingReader.ReadTerminatedStrings(3);
				if (Strings.Num() == 3)
				{
					Entries.Add({ Strings[0], Strings[1], Strings[2] });
				}
			}
		}
		return Entries;
	}

	/** The map's coarse ground classes against terrain/interior surfaceType names; the map itself never uses surfaceType. */
	FString GetEventMapDest(const FString& Surface)
	{
		static const TMap<FString, FString> DestBySurface = {
			{ TEXT("grass"), TEXT("grass") }, { TEXT("harddirt"), TEXT("dirt") }, { TEXT("softdirt"), TEXT("dirt") },
			{ TEXT("mud"), TEXT("dirt") }, { TEXT("rock"), TEXT("dirt") }, { TEXT("snow"), TEXT("dirt") },
			{ TEXT("sand"), TEXT("desert") }, { TEXT("stone"), TEXT("cement") }, { TEXT("surf"), TEXT("water") },
			{ TEXT("water"), TEXT("water") }, { TEXT("carpet"), TEXT("interior") }, { TEXT("wood"), TEXT("interior") },
			{ TEXT("metal"), TEXT("interior") } };
		const FString* Dest = DestBySurface.Find(Surface);
		return Dest ? *Dest : FString();
	}

	void GatherEntries(const TArray<FSWGClientDataEvent>& Entries, const FString& EventName, TArray<FString>& OutTargets)
	{
		for (const FSWGClientDataEvent& Entry : Entries)
		{
			if (Entry.EventName.Equals(EventName, ESearchCase::IgnoreCase) && !Entry.Target.IsEmpty())
			{
				OutTargets.Add(Entry.Target);
			}
		}
	}
}

USWGCreatureSoundComponent::USWGCreatureSoundComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGCreatureSoundComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AttackFallbackTimer);
	}
	StopAttachedLoops();
	Super::EndPlay(EndPlayReason);
}

USWGAudioSubsystem* USWGCreatureSoundComponent::GetAudio() const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<USWGAudioSubsystem>() : nullptr;
}

USWGTreSubsystem* USWGCreatureSoundComponent::GetTre() const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
}

USkeletalMeshComponent* USWGCreatureSoundComponent::GetMeshComponent() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetMesh() : nullptr;
}

bool USWGCreatureSoundComponent::TryGetListenerLocation(FVector& OutLocation) const
{
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Controller)
	{
		return false;
	}
	OutLocation = Controller->GetFocalLocation();
	return true;
}

void USWGCreatureSoundComponent::SetClientData(const FSWGClientDataFile& InClientData)
{
	StopAttachedLoops();
	ClientData = InClientData;
	bHasClientData = true;

	USWGTreSubsystem* Tre = GetTre();
	for (const FString& LoopPath : ClientData.AttachedSounds)
	{
		if (Tre && Tre->FileExists(SWGAudio::NormalizeSoundPath(LoopPath)))
		{
			AttachedLoops.Add({ LoopPath, FSWGSoundHandle() });
		}
	}

	if (!AttachedLoops.IsEmpty() && GetWorld())
	{
		// Random first delay spreads a crowd's range checks over the interval.
		GetWorld()->GetTimerManager().SetTimer(LoopTimer, this, &USWGCreatureSoundComponent::UpdateAttachedLoops, LoopCheckSeconds, true, FMath::FRandRange(0.f, LoopCheckSeconds));
	}
}

void USWGCreatureSoundComponent::StopAttachedLoops()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoopTimer);
	}
	for (const FAttachedLoop& Loop : AttachedLoops)
	{
		Loop.Handle.Stop(0.f);
	}
	AttachedLoops.Reset();
}

void USWGCreatureSoundComponent::UpdateAttachedLoops()
{
	USWGAudioSubsystem* Audio = GetAudio();
	USkeletalMeshComponent* Mesh = GetMeshComponent();
	FVector ListenerLocation;
	if (!Audio || !Mesh || !TryGetListenerLocation(ListenerLocation))
	{
		return;
	}

	const float Distance = static_cast<float>(FVector::Dist(ListenerLocation, Mesh->GetComponentLocation()));
	for (FAttachedLoop& Loop : AttachedLoops)
	{
		const float Radius = Audio->GetAudibleRadius(Loop.Path);
		const bool bInRange = Radius < 0.f || Distance <= Radius;
		if (!Loop.bActive && bInRange)
		{
			Loop.Handle = Audio->PlaySoundAttached(Loop.Path, Mesh);
			Loop.bActive = true;
		}
		else if (Loop.bActive && Radius > 0.f && Distance > Radius * LoopStopMargin)
		{
			Loop.Handle.Stop(0.25f);
			Loop.bActive = false;
		}
	}
}

bool USWGCreatureSoundComponent::IsFireEvent(const FString& EventName)
{
	if (!EventName.StartsWith(TEXT("fire")) || EventName.Len() <= 4)
	{
		return false;
	}
	const FString Rest = EventName.RightChop(4);
	return Rest[0] == TEXT('_') || FChar::IsDigit(Rest[0]);
}

bool USWGCreatureSoundComponent::IsEffectPath(const FString& Target)
{
	return Target.EndsWith(TEXT(".cef"), ESearchCase::IgnoreCase) || Target.StartsWith(TEXT("clienteffect/"), ESearchCase::IgnoreCase);
}

void USWGCreatureSoundComponent::CollectDirectSoundPaths(const FString& EventName, TArray<FString>& OutPaths)
{
	FString Name = EventName;
	if (!Name.RemoveFromStart(TEXT("sound_")) || Name.IsEmpty())
	{
		return;
	}
	OutPaths.Add(FString::Printf(TEXT("sound/%s.snd"), *Name));
	OutPaths.Add(FString::Printf(TEXT("sound/cr_%s.snd"), *Name));
}

void USWGCreatureSoundComponent::CollectEventTargets(const FSWGClientDataFile& Data, const FString& EventName, const FString& Surface, TArray<FString>& OutTargets)
{
	if (EventName.StartsWith(TEXT("sound_")) || IsFireEvent(EventName))
	{
		return;
	}

	FString BaseName = EventName;
	BaseName.RemoveFromStart(TEXT("event_"));

	TArray<FString> Candidates;
	if (BaseName == TEXT("footstep") && !Surface.IsEmpty())
	{
		Candidates.Add(TEXT("footstep_") + Surface);
	}
	Candidates.Add(BaseName);

	// A surface variant wins outright; the generic entry is only the fallback.
	for (const FString& Candidate : Candidates)
	{
		GatherEntries(Data.SoundEvents, Candidate, OutTargets);
		GatherEntries(Data.EffectEvents, Candidate, OutTargets);
		GatherEntries(Data.PlayerEvents, Candidate, OutTargets);
		if (!OutTargets.IsEmpty())
		{
			return;
		}
	}
}

bool USWGCreatureSoundComponent::IsTargetAudible(const FString& Target, const FVector& ListenerLocation, const FVector& SourceLocation) const
{
	USWGAudioSubsystem* Audio = GetAudio();
	if (!Audio)
	{
		return false;
	}

	// Decide before anything is decoded: the audio subsystem decodes on first play, audible or not.
	const double Distance = FVector::Dist(ListenerLocation, SourceLocation);
	auto IsSoundAudible = [Audio, Distance](const FString& SoundPath)
	{
		const float Radius = Audio->GetAudibleRadius(SoundPath);
		return Radius < 0.f || (Radius > 0.f && Distance <= Radius);
	};

	if (!IsEffectPath(Target))
	{
		return IsSoundAudible(Target);
	}

	const TSharedPtr<const FSWGClientEffect> Effect = Audio->GetClientEffect(SWGAudio::NormalizeEffectPath(Target));
	if (!Effect.IsValid())
	{
		return false;
	}
	for (const FString& SoundPath : Effect->SoundPaths)
	{
		if (IsSoundAudible(SoundPath))
		{
			return true;
		}
	}
	return false;
}

bool USWGCreatureSoundComponent::PlayTarget(const FString& Target, USceneComponent* AttachTo)
{
	USWGAudioSubsystem* Audio = GetAudio();
	if (!Audio || !AttachTo)
	{
		return false;
	}

	if (IsEffectPath(Target))
	{
		USWGEffectSubsystem* Effects = USWGEffectSubsystem::Get(this);
		return Effects && Effects->PlayClientEffectAttached(Target, AttachTo) > 0;
	}
	return Audio->PlaySoundAttached(Target, AttachTo).IsValid();
}

void USWGCreatureSoundComponent::OnAnimEvent(FName EventName, USkeletalMeshComponent* MeshComp)
{
	const FString Name = EventName.ToString();
	if (IsFireEvent(Name))
	{
		PlayArmedAttack(ESWGAttackPhase::Fire, EventName);
	}
	else if (IsSwingEvent(Name))
	{
		PlayArmedAttack(ESWGAttackPhase::Swing, EventName);
	}
	else if (IsMeleeHitEvent(Name))
	{
		PlayArmedAttack(ESWGAttackPhase::Hit, EventName);
	}
	else
	{
		FireEvent(EventName);
	}
}

bool USWGCreatureSoundComponent::IsSwingEvent(const FString& EventName)
{
	return EventName.StartsWith(TEXT("swing_")) || EventName.StartsWith(TEXT("uswing_"));
}

bool USWGCreatureSoundComponent::IsMeleeHitEvent(const FString& EventName)
{
	return EventName.StartsWith(TEXT("hit_")) || EventName.StartsWith(TEXT("uhit_"));
}

void USWGCreatureSoundComponent::ArmAttackSounds(const FSWGAttackSounds& Sounds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ArmedAttack = Sounds;
	ArmedAttackExpiry = World->GetTimeSeconds() + AttackWindowSeconds;
	bAttackArmed = true;
	bArmedAttackPlayed = false;
	World->GetTimerManager().SetTimer(AttackFallbackTimer, this, &USWGCreatureSoundComponent::PlayAttackFallback, AttackFallbackSeconds, false);
}

USceneComponent* USWGCreatureSoundComponent::FindMuzzle() const
{
	const ASWGCreature* Creature = Cast<ASWGCreature>(GetOwner());
	const USWGEquipmentComponent* Equipment = Creature ? Creature->EquipmentComponent.Get() : nullptr;
	return Equipment ? Equipment->GetWeaponHardpoint(SWGWeaponHardpoint::Muzzle, ArmedAttack.WeaponId) : nullptr;
}

void USWGCreatureSoundComponent::PlayArmedAttack(ESWGAttackPhase Phase, FName EventName)
{
	UWorld* World = GetWorld();
	if (!World || !bAttackArmed || World->GetTimeSeconds() > ArmedAttackExpiry)
	{
		UE_LOG(LogSWGAudio, Verbose, TEXT("%s: %s with no armed attack"), *GetNameSafe(GetOwner()), *EventName.ToString());
		return;
	}

	if (Phase == ESWGAttackPhase::Hit && !ArmedAttack.bLanded)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (const double* Last = LastPlayedTime.Find(EventName); Last && Now - *Last < MinRepeatSeconds)
	{
		return;
	}

	LastPlayedTime.Add(EventName, Now);
	bArmedAttackPlayed = true;

	const FString& Target = Phase == ESWGAttackPhase::Fire ? ArmedAttack.FireEffect : Phase == ESWGAttackPhase::Swing ? ArmedAttack.SwingEffect : ArmedAttack.HitEffect;
	if (!Target.IsEmpty())
	{
		// The shot leaves the muzzle, so its flash and sound do too.
		PlayCombatEffect(Target, Phase == ESWGAttackPhase::Fire ? FindMuzzle() : nullptr);
	}
	if (ArmedAttack.OnPhase)
	{
		ArmedAttack.OnPhase(Phase);
	}
}

void USWGCreatureSoundComponent::PlayAttackFallback()
{
	if (!bAttackArmed || bArmedAttackPlayed)
	{
		return;
	}

	// No fire/swing/hit message came with the clip (none authored, or it never played): sound the swing anyway.
	bArmedAttackPlayed = true;
	if (!ArmedAttack.FireEffect.IsEmpty())
	{
		PlayCombatEffect(ArmedAttack.FireEffect, FindMuzzle());
	}
	else
	{
		PlayCombatEffect(ArmedAttack.SwingEffect);
		if (ArmedAttack.bLanded)
		{
			PlayCombatEffect(ArmedAttack.HitEffect);
		}
	}
	if (ArmedAttack.OnPhase)
	{
		ArmedAttack.OnPhase(ArmedAttack.bRanged ? ESWGAttackPhase::Fire : ESWGAttackPhase::Swing);
	}
}

bool USWGCreatureSoundComponent::PlayCombatEffect(const FString& Target, USceneComponent* AttachTo)
{
	USWGTreSubsystem* Tre = GetTre();
	USkeletalMeshComponent* Mesh = GetMeshComponent();
	if (!Tre || !Mesh || !GetAudio() || Target.IsEmpty())
	{
		return false;
	}

	if (!Tre->FileExists(IsEffectPath(Target) ? SWGAudio::NormalizeEffectPath(Target) : SWGAudio::NormalizeSoundPath(Target)))
	{
		return false;
	}

	USceneComponent* Source = AttachTo ? AttachTo : Mesh;
	FVector ListenerLocation;
	if (TryGetListenerLocation(ListenerLocation) && !IsTargetAudible(Target, ListenerLocation, Source->GetComponentLocation()))
	{
		return false;
	}
	return PlayTarget(Target, Source);
}

bool USWGCreatureSoundComponent::FireEvent(FName EventName)
{
	UWorld* World = GetWorld();
	USWGTreSubsystem* Tre = GetTre();
	USkeletalMeshComponent* Mesh = GetMeshComponent();
	FVector ListenerLocation;
	if (!World || !Tre || !Mesh || !GetAudio())
	{
		return false;
	}

	const double Now = World->GetTimeSeconds();
	if (const double* Last = LastPlayedTime.Find(EventName); Last && Now - *Last < MinRepeatSeconds)
	{
		return false;
	}

	const FVector SourceLocation = Mesh->GetComponentLocation();
	const bool bHasListener = TryGetListenerLocation(ListenerLocation);
	if (bHasListener && FVector::Dist(ListenerLocation, SourceLocation) > MaxEventDistance)
	{
		return false;
	}

	const FString Name = EventName.ToString();
	TArray<FString> Targets;
	if (Name.StartsWith(TEXT("sound_")))
	{
		CollectDirectSoundPaths(Name, Targets);
	}
	else
	{
		FString BaseName = Name;
		BaseName.RemoveFromStart(TEXT("event_"));
		const bool bIsFootstep = BaseName == TEXT("footstep");
		const FString Surface = bIsFootstep ? ResolveSurface() : FString();
		CollectEventTargets(ClientData, Name, Surface, Targets);

		if (Targets.IsEmpty() && bIsFootstep && !Surface.IsEmpty())
		{
			// No footstep anywhere in the cdf: the retail source/dest map names a generic effect per ground class.
			const FString Dest = GetEventMapDest(Surface);
			for (const FEventMapEntry& Entry : GetEventMap(*Tre))
			{
				if (Entry.Source == TEXT("EVENT_SOURCE_footstep") && Entry.Dest == TEXT("EVENT_DEST_") + Dest)
				{
					Targets.Add(Entry.Target);
				}
			}
		}
	}

	// Retail data has many dangling targets; drop them silently rather than warn per footstep.
	Targets.RemoveAll([Tre](const FString& Target)
		{
			return !Tre->FileExists(IsEffectPath(Target) ? SWGAudio::NormalizeEffectPath(Target) : SWGAudio::NormalizeSoundPath(Target));
		});
	if (Targets.IsEmpty())
	{
		UE_LOG(LogSWGAudio, Verbose, TEXT("%s: no sound for event %s"), *GetNameSafe(GetOwner()), *Name);
		return false;
	}

	const FString& Chosen = Targets[FMath::RandRange(0, Targets.Num() - 1)];
	if (bHasListener && !IsTargetAudible(Chosen, ListenerLocation, SourceLocation))
	{
		return false;
	}

	LastPlayedTime.Add(EventName, Now);
	return PlayTarget(Chosen, Mesh);
}

FString USWGCreatureSoundComponent::ResolveSurface()
{
	const ASWGCreature* Creature = Cast<ASWGCreature>(GetOwner());
	if (Creature && Creature->PlacedInCell.IsValid())
	{
		return LookupInteriorSurface(Creature->PlacedInCell.Get());
	}

	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	USWGTerrainSubsystem* Terrain = GameInstance ? GameInstance->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
	return Terrain && GetOwner() ? Terrain->GetSurfaceTypeAt(GetOwner()->GetActorLocation()) : FString();
}

FString USWGCreatureSoundComponent::LookupInteriorSurface(ASWGCell* Cell)
{
	if (InteriorCell == Cell)
	{
		return InteriorSurface;
	}
	InteriorCell = Cell;
	InteriorSurface.Reset();

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (const FSWGInteriorRow* Row = GameInstance ? FSWGInteriorTable::FindForCell(Cell, *GameInstance) : nullptr)
	{
		InteriorSurface = Row->SurfaceType;
	}
	return InteriorSurface;
}
