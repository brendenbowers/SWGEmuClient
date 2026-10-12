#include "Subsystems/SWGCombatAudioSubsystem.h"
#include "SWGLogCategories.h"

#include "Common/SWGWorldScale.h"
#include "Components/SWGCombatStateComponent.h"
#include "Components/SWGCreatureSoundComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "GameFramework/Character.h"
#include "Network/Messages/Zone/Object/CombatActionIn.h"
#include "Objects/Creature/SWGCreature.h"
#include "SWGAudioSubsystem.h"
#include "Subsystems/SWGCombatSubsystem.h"
#include "Subsystems/SWGCombatVisualsSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGCombatManagerReader.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGEffectTables.h"

namespace
{
	const TCHAR* WeaponTablePath = TEXT("datatables/weapon/weapon.iff");

	// combat_manager variable tags, as stored (written backwards): SNDB, SNDL, SNDE, ATTL, WOTN.
	const FString VarBeginSound = TEXT("BDNS");
	const FString VarLoopSound = TEXT("LDNS");
	const FString VarEndSound = TEXT("EDNS");
	const FString VarEffectSeconds = TEXT("LTTA");
	const FString VarTempWeapon = TEXT("NTOW");

	// The playback scripts' own default (use_particle_system.pst) when an entry sets no LTTA.
	constexpr float DefaultEffectSeconds = 3.f;

	// Anim events fire a frame or two into the clip; the shot leaves just after.
	constexpr float MuzzleLeadSeconds = 0.05f;

	// How long after a landed melee swing its "hit" sounds, when the test command has no clip event to time it.
	constexpr float TestHitDelaySeconds = 0.3f;

	const FString& FirstEvent(const TArray<FSWGClientDataEvent>& Events, const TCHAR* EventName)
	{
		static const FString Empty;
		for (const FSWGClientDataEvent& Event : Events)
		{
			if (Event.EventName.Equals(EventName, ESearchCase::IgnoreCase) && !Event.Target.IsEmpty())
			{
				return Event.Target;
			}
		}
		return Empty;
	}

	USWGCreatureSoundComponent* FindSoundComponent(const USWGObjectGraphSubsystem* ObjectGraph, int64 ObjectId)
	{
		const ASWGCreature* Creature = ObjectGraph ? Cast<ASWGCreature>(ObjectGraph->FindActor(ObjectId)) : nullptr;
		return Creature ? Creature->CreatureSoundComponent.Get() : nullptr;
	}

	float FlightSeconds(const AActor* From, const AActor* To)
	{
		if (!From || !To)
		{
			return 0.f;
		}
		return FVector::Dist(From->GetActorLocation(), To->GetActorLocation()) / (USWGCombatAudioSubsystem::ProjectileSpeedMetres * SWGWorldScale);
	}
}

void USWGCombatAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Combat = Cast<USWGCombatSubsystem>(Collection.InitializeDependency(USWGCombatSubsystem::StaticClass()));
	ObjectGraph = Cast<USWGObjectGraphSubsystem>(Collection.InitializeDependency(USWGObjectGraphSubsystem::StaticClass()));
	Tre = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));
	Audio = Cast<USWGAudioSubsystem>(Collection.InitializeDependency(USWGAudioSubsystem::StaticClass()));
	Visuals = Cast<USWGCombatVisualsSubsystem>(Collection.InitializeDependency(USWGCombatVisualsSubsystem::StaticClass()));

	if (Combat)
	{
		ResolvedHandle = Combat->OnCombatActionResolved.AddUObject(this, &USWGCombatAudioSubsystem::HandleCombatActionResolved);
	}
}

void USWGCombatAudioSubsystem::Deinitialize()
{
	if (Combat && ResolvedHandle.IsValid())
	{
		Combat->OnCombatActionResolved.Remove(ResolvedHandle);
		ResolvedHandle.Reset();
	}

	WeaponTable.Reset();
	WeaponSoundCache.Reset();
	bWeaponTableLoadAttempted = false;
	Combat = nullptr;
	ObjectGraph = nullptr;
	Tre = nullptr;
	Visuals = nullptr;
	Audio = nullptr;
	Super::Deinitialize();
}

bool USWGCombatAudioSubsystem::ResolveWeaponSounds(USWGTreSubsystem& Tre, const FSWGDataTableData& WeaponTable, const FString& WeaponTemplate, FSWGWeaponSoundSet& OutSounds)
{
	OutSounds = FSWGWeaponSoundSet();
	OutSounds.WeaponTemplate = WeaponTemplate;
	if (WeaponTemplate.IsEmpty())
	{
		return false;
	}

	constexpr FSWGIffTag WeaponForm = SWG_IFF_TAG('S', 'W', 'O', 'T');
	constexpr FSWGIffTag ObjectForm = SWG_IFF_TAG('S', 'H', 'O', 'T');

	// weaponEffectIndex unset in the whole chain is the base template's 0.
	int32 Index = 0;
	Tre.FindTemplateFormStringParam(WeaponTemplate, WeaponForm, TEXT("weaponEffect"), OutSounds.EffectId);
	Tre.FindTemplateFormIntParam(WeaponTemplate, WeaponForm, TEXT("weaponEffectIndex"), Index);
	OutSounds.EffectIndex = Index;

	for (int32 RowIndex = 0; RowIndex < WeaponTable.Rows.Num(); ++RowIndex)
	{
		if (WeaponTable.GetCell(RowIndex, TEXT("Weapon Effect Id")) == OutSounds.EffectId
			&& WeaponTable.GetCell(RowIndex, TEXT("Weapon Effect Index")) == FString::FromInt(Index))
		{
			OutSounds.FireEffect = WeaponTable.GetCell(RowIndex, TEXT("Fire Client Effect"));
			OutSounds.ProjectileEffect = FSWGEffectTables::FindProjectileAppearance(WeaponTable, OutSounds.EffectId, Index);
			OutSounds.HitCreatureEffect = WeaponTable.GetCell(RowIndex, TEXT("Hit (Creature) Client Effect"));
			OutSounds.MissTerrainEffect = WeaponTable.GetCell(RowIndex, TEXT("Miss (Hit Terrain) Client Effect"));
			OutSounds.RicochetEffect = WeaponTable.GetCell(RowIndex, TEXT("Ricochet Client Effect"));
			break;
		}
	}

	FString ClientDataPath;
	FSWGClientDataFile ClientData;
	if (Tre.FindTemplateFormStringParam(WeaponTemplate, ObjectForm, TEXT("clientDataFile"), ClientDataPath)
		&& FSWGClientDataFileReader::Read(Tre.CreateIffReader(ClientDataPath), ClientData))
	{
		OutSounds.SwingEffect = FirstEvent(ClientData.EffectEvents, TEXT("swing"));
		OutSounds.HitEffect = FirstEvent(ClientData.EffectEvents, TEXT("hit"));
	}

	return !OutSounds.FireEffect.IsEmpty() || !OutSounds.SwingEffect.IsEmpty() || !OutSounds.ProjectileEffect.IsEmpty();
}

const FString& USWGCombatAudioSubsystem::PickImpactEffect(const FSWGWeaponSoundSet& Sounds, uint8 HitResult)
{
	static const FString Empty;
	switch (static_cast<ESWGCombatHit>(HitResult))
	{
		case ESWGCombatHit::Hit:
			return Sounds.HitCreatureEffect;
		// The bolt flies on past the target; with no collision query here it is taken to land on terrain.
		case ESWGCombatHit::Miss:
		case ESWGCombatHit::Dodge:
		case ESWGCombatHit::Fumble:
			return Sounds.MissTerrainEffect;
		case ESWGCombatHit::Ricochet:
		case ESWGCombatHit::Reflect:
		case ESWGCombatHit::ReflectToTarget:
			return Sounds.RicochetEffect;
		default:
			return Empty;
	}
}

const FSWGWeaponSoundSet* USWGCombatAudioSubsystem::GetWeaponSounds(const FString& WeaponTemplate)
{
	if (!Tre || WeaponTemplate.IsEmpty())
	{
		return nullptr;
	}

	if (const TSharedPtr<FSWGWeaponSoundSet>* Cached = WeaponSoundCache.Find(WeaponTemplate))
	{
		return Cached->Get();
	}

	if (!bWeaponTableLoadAttempted)
	{
		bWeaponTableLoadAttempted = true;
		WeaponTable = MakeShared<FSWGDataTableData>();
		if (!FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(WeaponTablePath), *WeaponTable))
		{
			UE_LOG(LogSWGCombat, Warning, TEXT("could not read %s - weapon fire and impact sounds are off"), WeaponTablePath);
			WeaponTable.Reset();
		}
	}

	TSharedPtr<FSWGWeaponSoundSet> Resolved;
	FSWGWeaponSoundSet Sounds;
	if (WeaponTable.IsValid() && ResolveWeaponSounds(*Tre, *WeaponTable, WeaponTemplate, Sounds))
	{
		Resolved = MakeShared<FSWGWeaponSoundSet>(MoveTemp(Sounds));
	}
	WeaponSoundCache.Add(WeaponTemplate, Resolved);
	return Resolved.Get();
}

void USWGCombatAudioSubsystem::PlayCombatEffectLater(USWGCreatureSoundComponent* Sound, const FString& Effect, float DelaySeconds)
{
	if (!Sound || Effect.IsEmpty())
	{
		return;
	}

	if (DelaySeconds <= 0.f)
	{
		Sound->PlayCombatEffect(Effect);
		return;
	}

	TWeakObjectPtr<USWGCreatureSoundComponent> WeakSound(Sound);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakSound, Effect](float)
		{
			if (USWGCreatureSoundComponent* LiveSound = WeakSound.Get())
			{
				LiveSound->PlayCombatEffect(Effect);
			}
			return false;
		}), DelaySeconds);
}

void USWGCombatAudioSubsystem::HandleCombatActionResolved(const FCombatActionIn& Action, const TArray<FSWGResolvedCombatDefender>& Defenders)
{
	const FSWGCombatActionAnimation* Script = nullptr;
	bool bLanded = false;
	for (const FSWGResolvedCombatDefender& Resolved : Defenders)
	{
		Script = Script ? Script : Resolved.Animation;
		bLanded |= Resolved.Defender && Resolved.Defender->Hit == static_cast<uint8>(ESWGCombatHit::Hit);
	}

	AActor* Attacker = ObjectGraph ? ObjectGraph->FindActor(Action.AttackerId) : nullptr;
	if (!Script || !Attacker || !Combat)
	{
		return;
	}
	USWGCreatureSoundComponent* AttackerSound = FindSoundComponent(ObjectGraph, Action.AttackerId);

	// fire_temp_weapon scripts conjure the weapon (NTOW) for the shot; otherwise it is the wielded one.
	const FString& TempWeapon = Script->GetVariable(VarTempWeapon);
	const FString WeaponTemplate = TempWeapon.IsEmpty() ? Combat->ResolveWeaponTemplatePath(Action.WeaponId) : TempWeapon;
	const FSWGWeaponSoundSet* Weapon = GetWeaponSounds(WeaponTemplate);

	// Every fire_* script launches a projectile (muzzle sound, then an impact); the rest are melee-style.
	const bool bProjectile = Script->PlaybackScript.StartsWith(TEXT("playback/fire_"));

	// What the clip's fire / swing event sets off besides sound: the bolt and its impact, or the swing ribbon.
	TFunction<void(ESWGAttackPhase)> OnPhase;
	if (Visuals && Weapon)
	{
		TArray<FSWGProjectileTarget> Targets;
		for (const FSWGResolvedCombatDefender& Resolved : Defenders)
		{
			if (Resolved.Defender)
			{
				Targets.Add({ Resolved.Defender->ObjectId, Resolved.Defender->Hit });
			}
		}
		OnPhase = [WeakVisuals = TWeakObjectPtr<USWGCombatVisualsSubsystem>(Visuals), AttackerId = Action.AttackerId, WeaponId = static_cast<uint64>(Action.WeaponId),
			WeaponSounds = *Weapon, Targets = MoveTemp(Targets), SpeedMetres = USWGCombatVisualsSubsystem::ResolveProjectileSpeed(*Script), TrailsMask = Action.Trails, bProjectile](ESWGAttackPhase Phase)
		{
			USWGCombatVisualsSubsystem* LiveVisuals = WeakVisuals.Get();
			if (!LiveVisuals)
			{
				return;
			}
			if (Phase == ESWGAttackPhase::Fire && bProjectile)
			{
				LiveVisuals->LaunchProjectiles(AttackerId, WeaponId, WeaponSounds, SpeedMetres, Targets);
			}
			else if (Phase == ESWGAttackPhase::Swing && !bProjectile)
			{
				LiveVisuals->BeginSwingTrail(AttackerId, WeaponId, WeaponSounds.WeaponTemplate, TrailsMask);
			}
		};
	}

	if (AttackerSound)
	{
		FSWGAttackSounds Armed;
		Armed.bLanded = bLanded;
		Armed.bRanged = bProjectile;
		Armed.WeaponId = Action.WeaponId;
		Armed.OnPhase = MoveTemp(OnPhase);
		if (bProjectile)
		{
			Armed.FireEffect = Weapon ? Weapon->FireEffect : FString();
		}
		else
		{
			Armed.SwingEffect = Weapon ? Weapon->SwingEffect : FString();
			Armed.HitEffect = Weapon ? Weapon->HitEffect : FString();

			// Unarmed (or a gun used as a club): the creature's own cdf names its swing and hit.
			if (Armed.SwingEffect.IsEmpty() && Armed.HitEffect.IsEmpty())
			{
				Armed.SwingEffect = FirstEvent(AttackerSound->GetClientData().EffectEvents, TEXT("swing"));
				Armed.HitEffect = FirstEvent(AttackerSound->GetClientData().EffectEvents, TEXT("hit"));
			}
		}
		AttackerSound->ArmAttackSounds(Armed);
	}
	else if (OnPhase)
	{
		OnPhase(bProjectile ? ESWGAttackPhase::Fire : ESWGAttackPhase::Swing);
	}

	PlayScriptSounds(*Script, *Attacker, AttackerSound);
}

void USWGCombatAudioSubsystem::PlayScriptSounds(const FSWGCombatActionAnimation& Animation, AActor& Attacker, USWGCreatureSoundComponent* AttackerSound)
{
	const FString& BeginSound = Animation.GetVariable(VarBeginSound);
	const FString& LoopSound = Animation.GetVariable(VarLoopSound);
	const FString& EndSound = Animation.GetVariable(VarEndSound);
	if (!AttackerSound || (BeginSound.IsEmpty() && LoopSound.IsEmpty() && EndSound.IsEmpty()))
	{
		return;
	}

	float Seconds = DefaultEffectSeconds;
	const float ParsedSeconds = FCString::Atof(*Animation.GetVariable(VarEffectSeconds));
	if (ParsedSeconds > 0.f)
	{
		Seconds = ParsedSeconds;
	}

	AttackerSound->PlayCombatEffect(BeginSound);

	// The loop runs for the effect's lifetime, then the end sound closes it.
	FSWGSoundHandle LoopHandle;
	const ACharacter* Character = Cast<ACharacter>(&Attacker);
	if (Audio && !LoopSound.IsEmpty() && Character && Character->GetMesh())
	{
		LoopHandle = Audio->PlaySoundAttached(LoopSound, Character->GetMesh());
	}

	TWeakObjectPtr<USWGCreatureSoundComponent> WeakSound(AttackerSound);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([LoopHandle, WeakSound, EndSound](float)
		{
			LoopHandle.Stop(0.25f);
			if (USWGCreatureSoundComponent* LiveSound = WeakSound.Get())
			{
				LiveSound->PlayCombatEffect(EndSound);
			}
			return false;
		}), Seconds);
}

bool USWGCombatAudioSubsystem::PlayTestSwing(const FString& WeaponTemplate, uint8 HitResult, FString& OutReport)
{
	const int64 PlayerId = ObjectGraph ? ObjectGraph->GetLocalPlayerObjectId() : 0;
	USWGCreatureSoundComponent* PlayerSound = FindSoundComponent(ObjectGraph, PlayerId);
	const FSWGWeaponSoundSet* Weapon = GetWeaponSounds(WeaponTemplate);
	if (!PlayerSound || !Weapon)
	{
		OutReport = PlayerSound ? FString::Printf(TEXT("%s resolves to no weapon sounds"), *WeaponTemplate) : FString(TEXT("no local player to play on"));
		return false;
	}

	OutReport = FString::Printf(TEXT("%s: effect %s #%d fire='%s' hitCreature='%s' missTerrain='%s' ricochet='%s' swing='%s' hit='%s'"),
		*WeaponTemplate, *Weapon->EffectId, Weapon->EffectIndex, *Weapon->FireEffect, *Weapon->HitCreatureEffect,
		*Weapon->MissTerrainEffect, *Weapon->RicochetEffect, *Weapon->SwingEffect, *Weapon->HitEffect);

	if (!Weapon->SwingEffect.IsEmpty())
	{
		PlayerSound->PlayCombatEffect(Weapon->SwingEffect);
		if (HitResult == static_cast<uint8>(ESWGCombatHit::Hit))
		{
			PlayCombatEffectLater(PlayerSound, Weapon->HitEffect, TestHitDelaySeconds);
		}
		return true;
	}

	// The player's target (else the player) takes the impact, as a real shot would.
	int64 TargetId = PlayerId;
	if (const USWGCombatStateComponent* CombatState = ObjectGraph->FindComponent<USWGCombatStateComponent>(PlayerId))
	{
		TargetId = CombatState->TargetId != 0 ? CombatState->TargetId : PlayerId;
	}

	PlayerSound->PlayCombatEffect(Weapon->FireEffect);
	const float Delay = MuzzleLeadSeconds + FlightSeconds(ObjectGraph->FindActor(PlayerId), ObjectGraph->FindActor(TargetId));
	PlayCombatEffectLater(FindSoundComponent(ObjectGraph, TargetId), PickImpactEffect(*Weapon, HitResult), Delay);
	return true;
}
