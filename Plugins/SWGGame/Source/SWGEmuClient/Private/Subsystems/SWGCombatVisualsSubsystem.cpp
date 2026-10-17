#include "Subsystems/SWGCombatVisualsSubsystem.h"
#include "SWGLogCategories.h"

#include "Common/SWGWeaponHardpoints.h"
#include "Common/SWGWorldScale.h"
#include "Components/CapsuleComponent.h"
#include "Components/SWGCombatStateComponent.h"
#include "Components/SWGEquipmentComponent.h"
#include "Components/SWGTrailComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Network/Messages/Zone/Object/CombatActionIn.h"
#include "Objects/Creature/SWGCreature.h"
#include "SWGEffectsSubsystem.h"
#include "SWGParticleEffectComponent.h"
#include "Subsystems/SWGCombatAudioSubsystem.h"
#include "Subsystems/SWGCombatSubsystem.h"
#include "Subsystems/SWGEffectSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGCombatManagerReader.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGEffectTables.h"
#include "TRE/SWGLightningEffectReader.h"

namespace
{
	const TCHAR* SwooshTablePath = TEXT("datatables/weapon/swoosh.iff");

	// combat_manager variable tags, as stored: LEVP is the script's PVEL (projectile speed), LTTA ATTL (effect seconds),
	// TPAP / NTPA the particle and lightning appearances, TPHE / 2PHE the attacker hardpoints they ride.
	const FString VarProjectileSpeed = TEXT("LEVP");
	const FString VarEffectSeconds = TEXT("LTTA");
	const FString VarParticle = TEXT("TPAP");
	const FString VarLightning = TEXT("NTPA");
	const FString VarHardpoint = TEXT("TPHE");
	const FString VarSecondHardpoint = TEXT("2PHE");

	// The scripts' own default (use_particle_system.pst) when an entry sets no LTTA.
	constexpr float DefaultEffectSeconds = 3.f;

	constexpr float MinFlightSeconds = 0.02f;
	constexpr float MaxFlightSeconds = 3.f;

	// A bolt that misses lands this far past the defender, spread this far to either side (cm).
	constexpr float MissOvershootMin = 150.f;
	constexpr float MissOvershootMax = 400.f;
	constexpr float MissSpread = 60.f;

	// Without a muzzle the shot leaves the hand, else the chest.
	constexpr float FallbackMuzzleHeight = 120.f;

	// The console test waits this long for the debug weapon's mesh before firing.
	constexpr float TestMeshWaitSeconds = 1.5f;
	constexpr float TestTargetDistance = 1200.f;

	bool IsMissLike(uint8 HitResult)
	{
		switch (static_cast<ESWGCombatHit>(HitResult))
		{
			case ESWGCombatHit::Miss:
			case ESWGCombatHit::Dodge:
			case ESWGCombatHit::Fumble:
				return true;
			default:
				return false;
		}
	}

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

	/** Stops a particle effect after a while; a one-shot has ended by then and ignores it. */
	void StopParticleLater(const FSWGEffectHandle& Handle, float Seconds)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Handle](float)
		{
			Handle.Stop(false);
			return false;
		}), Seconds);
	}
}

void USWGCombatVisualsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Combat = Cast<USWGCombatSubsystem>(Collection.InitializeDependency(USWGCombatSubsystem::StaticClass()));
	ObjectGraph = Cast<USWGObjectGraphSubsystem>(Collection.InitializeDependency(USWGObjectGraphSubsystem::StaticClass()));
	Tre = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));

	if (Combat)
	{
		ResolvedHandle = Combat->OnCombatActionResolved.AddUObject(this, &USWGCombatVisualsSubsystem::HandleCombatActionResolved);
	}
}

void USWGCombatVisualsSubsystem::Deinitialize()
{
	if (Combat && ResolvedHandle.IsValid())
	{
		Combat->OnCombatActionResolved.Remove(ResolvedHandle);
		ResolvedHandle.Reset();
	}

	for (FProjectile& Projectile : Projectiles)
	{
		Projectile.Bolt.Stop(true);
	}
	Projectiles.Reset();
	TrailStops.Reset();
	Trails.Reset();
	SwooshTable.Reset();
	VisualsCache.Reset();
	Combat = nullptr;
	ObjectGraph = nullptr;
	Tre = nullptr;
	Super::Deinitialize();
}

TStatId USWGCombatVisualsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGCombatVisualsSubsystem, STATGROUP_Tickables);
}

float USWGCombatVisualsSubsystem::ProjectileFlightSeconds(float DistanceCm, float SpeedMetres)
{
	return SpeedMetres > 0.f ? DistanceCm / (SpeedMetres * SWGWorldScale) : 0.f;
}

bool USWGCombatVisualsSubsystem::WantsWeaponTrail(uint8 TrailsMask)
{
	return (TrailsMask & SWGCombatTrail::Weapon) != 0;
}

float USWGCombatVisualsSubsystem::ResolveProjectileSpeed(const FSWGCombatActionAnimation& Animation)
{
	const float Speed = FCString::Atof(*Animation.GetVariable(VarProjectileSpeed));
	return Speed > 0.f ? Speed : USWGCombatAudioSubsystem::ProjectileSpeedMetres;
}

float USWGCombatVisualsSubsystem::ParseEffectSeconds(const FString& Value, float DefaultSeconds)
{
	// One entry spells half a second ".0.5".
	FString Number = Value;
	if (Number.StartsWith(TEXT(".")) && Number.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1) != INDEX_NONE)
	{
		Number.RightChopInline(1);
	}
	const float Seconds = FCString::Atof(*Number);
	return Seconds > 0.f ? Seconds : DefaultSeconds;
}

const FSWGDataTableData* USWGCombatVisualsSubsystem::GetSwooshTable()
{
	if (!bSwooshTableLoadAttempted && Tre)
	{
		bSwooshTableLoadAttempted = true;
		SwooshTable = MakeShared<FSWGDataTableData>();
		if (!FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(SwooshTablePath), *SwooshTable))
		{
			UE_LOG(LogSWGCombat, Warning, TEXT("could not read %s - melee trails are off"), SwooshTablePath);
			SwooshTable.Reset();
		}
	}
	return SwooshTable.Get();
}

const FSWGWeaponVisuals* USWGCombatVisualsSubsystem::GetWeaponVisuals(const FString& WeaponTemplate)
{
	if (!Tre || WeaponTemplate.IsEmpty())
	{
		return nullptr;
	}
	if (const TSharedPtr<FSWGWeaponVisuals>* Cached = VisualsCache.Find(WeaponTemplate))
	{
		return Cached->Get();
	}

	TSharedRef<FSWGWeaponVisuals> Visuals = MakeShared<FSWGWeaponVisuals>();
	Visuals->WeaponTemplate = WeaponTemplate;

	if (const FSWGDataTableData* Table = GetSwooshTable())
	{
		FString SwooshPath;
		bool bUseObjectHue = false;
		if (FSWGEffectTables::FindSwoosh(*Table, WeaponTemplate, SwooshPath, bUseObjectHue)
			&& FSWGSwooshReader::Read(Tre->CreateIffReader(SwooshPath), Visuals->Swoosh))
		{
			Visuals->bHasSwoosh = true;
			Visuals->bSwooshUseObjectHue = bUseObjectHue;
			Visuals->SwooshPath = SwooshPath;
		}
	}

	FString ClientDataPath;
	FSWGClientDataFile ClientData;
	if (Tre->FindTemplateFormStringParam(WeaponTemplate, SWG_IFF_TAG('S', 'H', 'O', 'T'), TEXT("clientDataFile"), ClientDataPath)
		&& FSWGClientDataFileReader::Read(Tre->CreateIffReader(ClientDataPath), ClientData))
	{
		Visuals->BladeOnEffect = FirstEvent(ClientData.EffectEvents, TEXT("bladeon"));
		Visuals->BladeOffEffect = FirstEvent(ClientData.EffectEvents, TEXT("bladeoff"));
	}

	VisualsCache.Add(WeaponTemplate, Visuals);
	return &Visuals.Get();
}

FVector USWGCombatVisualsSubsystem::ResolveHitPoint(const AActor& Defender)
{
	// A character's capsule is centred on its body.
	if (const ACharacter* Character = Cast<ACharacter>(&Defender))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			return Capsule->GetComponentLocation();
		}
	}
	return Defender.GetComponentsBoundingBox().GetCenter();
}

USceneComponent* USWGCombatVisualsSubsystem::ResolveAttachPoint(AActor& Actor, const FString& Hardpoint, uint64 WeaponId, FName& OutSocket) const
{
	OutSocket = NAME_None;
	const FName Name(*Hardpoint);
	if (const ASWGCreature* Creature = Cast<ASWGCreature>(&Actor))
	{
		if (const USWGEquipmentComponent* Equipment = Creature->EquipmentComponent.Get())
		{
			if (USceneComponent* Point = Equipment->GetWeaponHardpoint(Name, WeaponId))
			{
				return Point;
			}
		}
	}
	if (const ACharacter* Character = Cast<ACharacter>(&Actor))
	{
		if (USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			if (!Name.IsNone() && Mesh->DoesSocketExist(Name))
			{
				OutSocket = Name;
			}
			return Mesh;
		}
	}
	return Actor.GetRootComponent();
}

FVector USWGCombatVisualsSubsystem::ResolveMuzzleLocation(AActor& Attacker, uint64 WeaponId) const
{
	if (const ASWGCreature* Creature = Cast<ASWGCreature>(&Attacker))
	{
		if (const USceneComponent* Muzzle = Creature->EquipmentComponent ? Creature->EquipmentComponent->GetWeaponHardpoint(SWGWeaponHardpoint::Muzzle, WeaponId) : nullptr)
		{
			return Muzzle->GetComponentLocation();
		}
	}
	const FName HandSocket(TEXT("hold_r"));
	if (const ACharacter* Character = Cast<ACharacter>(&Attacker))
	{
		if (const USkeletalMeshComponent* Mesh = Character->GetMesh(); Mesh && Mesh->DoesSocketExist(HandSocket))
		{
			return Mesh->GetSocketLocation(HandSocket);
		}
	}
	return Attacker.GetActorLocation() + FVector::UpVector * FallbackMuzzleHeight;
}

void USWGCombatVisualsSubsystem::LaunchProjectiles(int64 AttackerId, uint64 WeaponId, const FSWGWeaponSoundSet& Weapon, float SpeedMetres, const TArray<FSWGProjectileTarget>& Targets)
{
	AActor* Attacker = ObjectGraph ? ObjectGraph->FindActor(AttackerId) : nullptr;
	UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const FVector Muzzle = ResolveMuzzleLocation(*Attacker, WeaponId);

	for (const FSWGProjectileTarget& Target : Targets)
	{
		const AActor* Defender = ObjectGraph->FindActor(Target.DefenderId);
		if (!Defender)
		{
			continue;
		}

		const FVector HitPoint = ResolveHitPoint(*Defender);
		FVector End = HitPoint;
		FRotator ImpactRotation = (Muzzle - HitPoint).Rotation();
		if (IsMissLike(Target.Hit))
		{
			// The bolt flies on past the defender and lands on whatever is under that line.
			const FVector Forward = (HitPoint - Muzzle).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(Forward, FVector::UpVector).GetSafeNormal();
			const FVector Past = HitPoint + Forward * FMath::FRandRange(MissOvershootMin, MissOvershootMax) + Side * FMath::FRandRange(-MissSpread, MissSpread);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(SWGMissBolt), false, Attacker);
			Params.AddIgnoredActor(Defender);
			FHitResult Ground;
			if (World->LineTraceSingleByChannel(Ground, Past + FVector::UpVector * 300.f, Past - FVector::UpVector * 2000.f, ECC_Visibility, Params))
			{
				End = Ground.ImpactPoint;
				ImpactRotation = FRotationMatrix::MakeFromZ(Ground.ImpactNormal).Rotator();
			}
			else
			{
				End = Past;
			}
		}

		SpawnProjectile(*World, Weapon, Muzzle, End, ImpactRotation, Target.Hit, SpeedMetres);
	}
}

void USWGCombatVisualsSubsystem::SpawnProjectile(UWorld& World, const FSWGWeaponSoundSet& Weapon, const FVector& Start, const FVector& End, const FRotator& ImpactRotation, uint8 HitResult, float SpeedMetres)
{
	FProjectile Projectile;
	Projectile.Start = Start;
	Projectile.End = End;
	Projectile.Duration = FMath::Clamp(ProjectileFlightSeconds(static_cast<float>(FVector::Dist(Start, End)), SpeedMetres), MinFlightSeconds, MaxFlightSeconds);
	Projectile.ImpactEffect = USWGCombatAudioSubsystem::PickImpactEffect(Weapon, HitResult);
	Projectile.ImpactRotation = ImpactRotation;

	if (!Weapon.ProjectileEffect.IsEmpty())
	{
		if (USWGEffectsSubsystem* Effects = World.GetSubsystem<USWGEffectsSubsystem>())
		{
			const FQuat Facing = FRotationMatrix::MakeFromX((End - Start).GetSafeNormal()).ToQuat();
			Projectile.Bolt = Effects->PlayParticleAtLocation(Weapon.ProjectileEffect, FTransform(Facing, Start));
		}
	}
	Projectiles.Add(MoveTemp(Projectile));
}

void USWGCombatVisualsSubsystem::Tick(float DeltaTime)
{
	for (int32 Index = Projectiles.Num() - 1; Index >= 0; --Index)
	{
		FProjectile& Projectile = Projectiles[Index];
		Projectile.Elapsed += DeltaTime;
		if (Projectile.Elapsed >= Projectile.Duration)
		{
			FinishProjectile(Projectile);
			Projectiles.RemoveAtSwap(Index);
		}
		else if (USWGParticleEffectComponent* Bolt = Projectile.Bolt.Effect.Get(); Bolt && Projectile.Bolt.IsValid())
		{
			Bolt->SetWorldLocation(FMath::Lerp(Projectile.Start, Projectile.End, Projectile.Elapsed / Projectile.Duration));
		}
	}

	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	for (int32 Index = TrailStops.Num() - 1; Index >= 0; --Index)
	{
		USWGTrailComponent* Trail = TrailStops[Index].Trail.Get();
		if (!Trail)
		{
			TrailStops.RemoveAtSwap(Index);
		}
		else if (Now >= TrailStops[Index].EndTime)
		{
			Trail->SetEmitting(false);
			TrailStops.RemoveAtSwap(Index);
		}
	}
}

void USWGCombatVisualsSubsystem::FinishProjectile(FProjectile& Projectile)
{
	Projectile.Bolt.Stop(true);
	if (Projectile.ImpactEffect.IsEmpty())
	{
		return;
	}
	if (USWGEffectSubsystem* Effects = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGEffectSubsystem>() : nullptr)
	{
		Effects->PlayClientEffectAtLocation(Projectile.ImpactEffect, Projectile.End, Projectile.ImpactRotation);
	}
}

void USWGCombatVisualsSubsystem::BeginSwingTrail(int64 AttackerId, uint64 WeaponId, const FString& WeaponTemplate, uint8 TrailsMask)
{
	if (!WantsWeaponTrail(TrailsMask))
	{
		return;
	}

	ASWGCreature* Attacker = ObjectGraph ? Cast<ASWGCreature>(ObjectGraph->FindActor(AttackerId)) : nullptr;
	const USWGEquipmentComponent* Equipment = Attacker ? Attacker->EquipmentComponent.Get() : nullptr;
	const FSWGWeaponVisuals* Visuals = GetWeaponVisuals(WeaponTemplate);
	UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
	USWGEffectsSubsystem* Effects = World ? World->GetSubsystem<USWGEffectsSubsystem>() : nullptr;
	if (!Equipment || !Visuals || !Visuals->bHasSwoosh || !Effects)
	{
		return;
	}

	TArray<UMaterialInterface*> Materials;
	float SecondsPerFrame = 0.f;
	Effects->GetRibbonMaterials(Visuals->Swoosh.Texture.ShaderPath, Materials, SecondsPerFrame);

	FSWGTrailSet& Set = Trails.FindOrAdd(AttackerId);
	const bool bReconfigure = Set.ConfiguredFor != WeaponTemplate;
	const FName EdgeStarts[] = { SWGWeaponHardpoint::TrailStart, SWGWeaponHardpoint::SecondTrailStart };
	const FName EdgeEnds[] = { SWGWeaponHardpoint::TrailEnd, SWGWeaponHardpoint::SecondTrailEnd };
	for (int32 EdgeIndex = 0; EdgeIndex < UE_ARRAY_COUNT(EdgeStarts); ++EdgeIndex)
	{
		USceneComponent* StartPoint = Equipment->GetWeaponHardpoint(EdgeStarts[EdgeIndex], WeaponId);
		USceneComponent* EndPoint = Equipment->GetWeaponHardpoint(EdgeEnds[EdgeIndex], WeaponId);
		if (!StartPoint || !EndPoint)
		{
			continue;
		}

		while (Set.Edges.Num() <= EdgeIndex)
		{
			Set.Edges.Add(nullptr);
		}
		USWGTrailComponent* Trail = Set.Edges[EdgeIndex];
		const bool bCreated = Trail == nullptr;
		if (bCreated)
		{
			Trail = NewObject<USWGTrailComponent>(Attacker);
			Trail->RegisterComponent();
			Set.Edges[EdgeIndex] = Trail;
		}
		if (bCreated || bReconfigure)
		{
			Trail->Configure(Materials.IsEmpty() ? nullptr : Materials[0], Visuals->Swoosh.Color, Visuals->Swoosh.LifetimeSeconds);
		}
		Trail->SetEdge(StartPoint, EndPoint);
		Trail->SetEmitting(true);

		const double EndTime = World->GetTimeSeconds() + SwingTrailSeconds;
		FTrailStop* Existing = TrailStops.FindByPredicate([Trail](const FTrailStop& Stop) { return Stop.Trail == Trail; });
		if (Existing)
		{
			Existing->EndTime = EndTime;
		}
		else
		{
			TrailStops.Add({ Trail, EndTime });
		}
	}
	Set.ConfiguredFor = WeaponTemplate;
}

void USWGCombatVisualsSubsystem::HandleCombatActionResolved(const FCombatActionIn& Action, const TArray<FSWGResolvedCombatDefender>& Defenders)
{
	for (int32 DefenderIndex = 0; DefenderIndex < Defenders.Num(); ++DefenderIndex)
	{
		const FSWGResolvedCombatDefender& Resolved = Defenders[DefenderIndex];
		if (!Resolved.Animation)
		{
			continue;
		}

		const bool bParticle = Resolved.Animation->PlaybackScript.Contains(TEXT("particle"));
		const bool bLightning = Resolved.Animation->PlaybackScript.Contains(TEXT("lightning"));
		// A particle script plays once on the attacker however many defenders it hits; lightning goes to each.
		if ((bParticle && DefenderIndex == 0) || bLightning)
		{
			PlayParticleScript(Action, Resolved, *Resolved.Animation);
		}
	}
}

void USWGCombatVisualsSubsystem::PlayParticleScript(const FCombatActionIn& Action, const FSWGResolvedCombatDefender& Defender, const FSWGCombatActionAnimation& Script)
{
	AActor* Attacker = ObjectGraph ? ObjectGraph->FindActor(Action.AttackerId) : nullptr;
	UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
	USWGEffectsSubsystem* Effects = World ? World->GetSubsystem<USWGEffectsSubsystem>() : nullptr;
	if (!Effects)
	{
		return;
	}

	const bool bTwoParticles = Script.PlaybackScript.Contains(TEXT("2_particle"));
	const bool bLightning = Script.PlaybackScript.Contains(TEXT("lightning"));
	const float Seconds = ParseEffectSeconds(Script.GetVariable(VarEffectSeconds), DefaultEffectSeconds);

	// The scripts' own defaults: a lone particle rides the muzzle, a pair the two hands, lightning leaves the right hand.
	FString Hardpoint = Script.GetVariable(VarHardpoint);
	if (Hardpoint.IsEmpty())
	{
		Hardpoint = bLightning ? TEXT("hold_r") : bTwoParticles ? TEXT("hold_r") : SWGWeaponHardpoint::Muzzle.ToString();
	}
	FString SecondHardpoint = Script.GetVariable(VarSecondHardpoint);
	if (SecondHardpoint.IsEmpty())
	{
		SecondHardpoint = TEXT("hold_l");
	}

	auto PlayAt = [&](const FString& Prt, const FString& AttachName)
	{
		if (Prt.IsEmpty())
		{
			return;
		}
		FName Socket;
		if (USceneComponent* Point = ResolveAttachPoint(*Attacker, AttachName, static_cast<uint64>(Action.WeaponId), Socket))
		{
			StopParticleLater(Effects->PlayParticleAttached(Prt, Point, Socket), Seconds);
		}
	};

	if (!bLightning)
	{
		const FString& Prt = Script.GetVariable(VarParticle);
		PlayAt(Prt, Hardpoint);
		if (bTwoParticles)
		{
			PlayAt(Prt, SecondHardpoint);
		}
		return;
	}

	// Force lightning: the .ltn names a start effect on the caster's hand and an end effect on the target.
	FSWGLightningEffect Lightning;
	const FString LightningPath = Script.GetVariable(VarLightning);
	if (!Tre || LightningPath.IsEmpty() || !FSWGLightningEffectReader::Read(Tre->CreateIffReader(LightningPath), Lightning))
	{
		return;
	}
	PlayAt(Lightning.StartEffectPath, Hardpoint);

	const AActor* Target = Defender.Defender && ObjectGraph ? ObjectGraph->FindActor(Defender.Defender->ObjectId) : nullptr;
	if (Target && !Lightning.EndEffectPath.IsEmpty())
	{
		StopParticleLater(Effects->PlayParticleAtLocation(Lightning.EndEffectPath, FTransform(ResolveHitPoint(*Target))), Seconds);
	}

	// TODO: the bolt body itself (the .ltn's textured, waveform-displaced beam between hand and target) is not drawn yet.
	UE_LOG(LogSWGCombat, Verbose, TEXT("lightning %s: start and end effects only, the beam body is not drawn"), *LightningPath);
}

bool USWGCombatVisualsSubsystem::PlayTestShot(const FString& WeaponTemplate, uint8 HitResult, FString& OutReport)
{
	const int64 PlayerId = ObjectGraph ? ObjectGraph->GetLocalPlayerObjectId() : 0;
	ASWGCreature* Player = ObjectGraph ? Cast<ASWGCreature>(ObjectGraph->FindActor(PlayerId)) : nullptr;
	UGameInstance* GameInstance = GetGameInstance();
	USWGCombatAudioSubsystem* Audio = GameInstance ? GameInstance->GetSubsystem<USWGCombatAudioSubsystem>() : nullptr;
	const FSWGWeaponSoundSet* Weapon = Audio ? Audio->FindWeaponSounds(WeaponTemplate) : nullptr;
	if (!Player || !Player->EquipmentComponent || !Weapon || !Tre)
	{
		OutReport = FString::Printf(TEXT("%s: no local player or no weapon data"), *WeaponTemplate);
		return false;
	}

	FString Appearance;
	if (!Tre->FindTemplateFormStringParam(WeaponTemplate, SWG_IFF_TAG('S', 'H', 'O', 'T'), TEXT("appearanceFilename"), Appearance) || Appearance.IsEmpty())
	{
		OutReport = FString::Printf(TEXT("%s has no appearanceFilename"), *WeaponTemplate);
		return false;
	}
	Player->EquipmentComponent->AttachDebugItem(Appearance, WeaponTemplate);

	const bool bRanged = !Weapon->FireEffect.IsEmpty() || !Weapon->ProjectileEffect.IsEmpty();
	OutReport = FString::Printf(TEXT("%s: %s, projectile '%s', fire '%s', hit result %d; firing in %.1f s"), *WeaponTemplate,
		bRanged ? TEXT("ranged") : TEXT("melee"), *Weapon->ProjectileEffect, *Weapon->FireEffect, HitResult, TestMeshWaitSeconds);

	// The debug weapon's mesh builds asynchronously; fire once it should be on the hand.
	TWeakObjectPtr<USWGCombatVisualsSubsystem> WeakThis(this);
	FSWGWeaponSoundSet WeaponCopy = *Weapon;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, PlayerId, WeaponCopy, HitResult, bRanged](float)
	{
		USWGCombatVisualsSubsystem* Self = WeakThis.Get();
		ASWGCreature* LivePlayer = Self && Self->ObjectGraph ? Cast<ASWGCreature>(Self->ObjectGraph->FindActor(PlayerId)) : nullptr;
		UWorld* World = LivePlayer ? LivePlayer->GetWorld() : nullptr;
		if (!World)
		{
			return false;
		}

		if (!bRanged)
		{
			Self->BeginSwingTrail(PlayerId, 0, WeaponCopy.WeaponTemplate, 0xFF);
			return false;
		}

		if (USWGEffectSubsystem* Effects = World->GetGameInstance()->GetSubsystem<USWGEffectSubsystem>())
		{
			USceneComponent* Muzzle = LivePlayer->EquipmentComponent->GetWeaponHardpoint(SWGWeaponHardpoint::Muzzle);
			if (Muzzle && !WeaponCopy.FireEffect.IsEmpty())
			{
				Effects->PlayClientEffectAttached(WeaponCopy.FireEffect, Muzzle);
			}
		}

		// At the player's target, else a point ahead on the ground.
		const USWGCombatStateComponent* CombatState = LivePlayer->CombatStateComponent.Get();
		if (CombatState && CombatState->TargetId != 0 && Self->ObjectGraph->FindActor(CombatState->TargetId))
		{
			TArray<FSWGProjectileTarget> Targets;
			Targets.Add({ CombatState->TargetId, HitResult });
			Self->LaunchProjectiles(PlayerId, 0, WeaponCopy, USWGCombatAudioSubsystem::ProjectileSpeedMetres, Targets);
			return false;
		}

		const FVector Ahead = LivePlayer->GetActorLocation() + LivePlayer->GetActorForwardVector() * TestTargetDistance;
		FHitResult Ground;
		const FVector End = World->LineTraceSingleByChannel(Ground, Ahead + FVector::UpVector * 300.f, Ahead - FVector::UpVector * 2000.f, ECC_Visibility) ? FVector(Ground.ImpactPoint) : Ahead;
		const USceneComponent* MuzzlePoint = LivePlayer->EquipmentComponent->GetWeaponHardpoint(SWGWeaponHardpoint::Muzzle);
		const FVector Start = MuzzlePoint ? MuzzlePoint->GetComponentLocation() : LivePlayer->GetActorLocation() + FVector::UpVector * FallbackMuzzleHeight;
		Self->SpawnProjectile(*World, WeaponCopy, Start, End, FRotator::ZeroRotator, HitResult, USWGCombatAudioSubsystem::ProjectileSpeedMetres);
		return false;
	}), TestMeshWaitSeconds);
	return true;
}
