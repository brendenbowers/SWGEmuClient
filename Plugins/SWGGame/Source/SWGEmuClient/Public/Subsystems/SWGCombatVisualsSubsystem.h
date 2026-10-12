#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "SWGEffectsTypes.h"
#include "TRE/SWGSwooshReader.h"
#include "SWGCombatVisualsSubsystem.generated.h"

class AActor;
class UWorld;
class USceneComponent;
class USWGCombatSubsystem;
class USWGObjectGraphSubsystem;
class USWGTrailComponent;
class USWGTreSubsystem;
struct FCombatActionIn;
struct FSWGCombatActionAnimation;
struct FSWGDataTableData;
struct FSWGResolvedCombatDefender;
struct FSWGWeaponSoundSet;

/** What a weapon template looks like in a fight, besides its sounds: the swing ribbon and a saber's ignite / retract effects. */
struct FSWGWeaponVisuals
{
	FString WeaponTemplate;

	/** swoosh.iff -> .swh. UseObjectHue weapons (crafted sabers) would take the object's colour; the .swh colour stands in. */
	bool bHasSwoosh = false;
	bool bSwooshUseObjectHue = false;
	FString SwooshPath;
	FSWGSwoosh Swoosh;

	/** The weapon cdf's bladeon / bladeoff clienteffects (lightsabers). */
	FString BladeOnEffect;
	FString BladeOffEffect;
};

/** One defender a shot is aimed at, and the ESWGCombatHit it ends in. */
struct FSWGProjectileTarget
{
	int64 DefenderId = 0;
	uint8 Hit = 0;
};

/** One attacker's swing ribbons: an edge per trail hardpoint pair (tr1s-tr1e, tr2s-tr2e), configured for ConfiguredFor. */
USTRUCT()
struct FSWGTrailSet
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<USWGTrailComponent>> Edges;

	UPROPERTY()
	FString ConfiguredFor;
};

/**
 * Combat visuals that hang off a CombatAction: the projectile bolt that flies from the weapon's muzzle to each
 * defender (and plays the impact effect on arrival, so sound and picture land together), the melee swing ribbon
 * on the weapon's trail hardpoints, and the particle / lightning playback scripts of flamethrowers and Force powers.
 * USWGCombatAudioSubsystem arms the attacker; the clip's fire / swing events call back here.
 */
UCLASS()
class SWGEMUCLIENT_API USWGCombatVisualsSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !Projectiles.IsEmpty() || !TrailStops.IsEmpty(); }

	/** Seconds a projectile takes to cover DistanceCm at SpeedMetres per second. */
	static float ProjectileFlightSeconds(float DistanceCm, float SpeedMetres);

	/** Whether the CombatAction Trails mask asks for a weapon ribbon (0xFF, "default", includes it). */
	static bool WantsWeaponTrail(uint8 TrailsMask);

	/** The metres-per-second a playback entry sets (LEVP, from its FLOT variable), else the engine default of 55. */
	static float ResolveProjectileSpeed(const FSWGCombatActionAnimation& Animation);

	/** A playback script's LTTA seconds; tolerates the data's ".0.5" spelling. Falls back to DefaultSeconds. */
	static float ParseEffectSeconds(const FString& Value, float DefaultSeconds);

	/** Swoosh and saber effects for a weapon template, cached. Null when the template is empty. */
	const FSWGWeaponVisuals* GetWeaponVisuals(const FString& WeaponTemplate);

	/**
	 * Fires the weapon's projectile at each target from the attacker's muzzle: a bolt .prt flies at SpeedMetres and
	 * the hit / miss / ricochet client effect plays where it ends. Without a .prt the shot is still timed and lands.
	 */
	void LaunchProjectiles(int64 AttackerId, uint64 WeaponId, const FSWGWeaponSoundSet& Weapon, float SpeedMetres, const TArray<FSWGProjectileTarget>& Targets);

	/** Starts the attacker's weapon ribbon for a swing; it fades out after the swing window. */
	void BeginSwingTrail(int64 AttackerId, uint64 WeaponId, const FString& WeaponTemplate, uint8 TrailsMask);

	/**
	 * Console test: puts the weapon in the local player's hand and, once its mesh is up, fires it at the target (or
	 * a point ahead) / swings it. Fills OutReport.
	 */
	bool PlayTestShot(const FString& WeaponTemplate, uint8 HitResult, FString& OutReport);

	/** Seconds the swing ribbon keeps recording after the swing event. */
	static constexpr float SwingTrailSeconds = 0.6f;

private:
	struct FProjectile
	{
		FSWGEffectHandle Bolt;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		float Elapsed = 0.f;
		float Duration = 0.f;
		FString ImpactEffect;
		FRotator ImpactRotation = FRotator::ZeroRotator;
	};

	void HandleCombatActionResolved(const FCombatActionIn& Action, const TArray<FSWGResolvedCombatDefender>& Defenders);

	/** Particle playback scripts: TPAP (and 2PHE's second) on the attacker's hardpoints for LTTA seconds, lightning NTPA to the defender. */
	void PlayParticleScript(const FCombatActionIn& Action, const FSWGResolvedCombatDefender& Defender, const FSWGCombatActionAnimation& Script);

	void FinishProjectile(FProjectile& Projectile);

	/** Starts one bolt (if the weapon has a projectile .prt) from Start to End; the impact effect for HitResult plays at End on arrival. */
	void SpawnProjectile(UWorld& World, const FSWGWeaponSoundSet& Weapon, const FVector& Start, const FVector& End, const FRotator& ImpactRotation, uint8 HitResult, float SpeedMetres);

	/** The attacker's muzzle hardpoint, else its hand, else a point at chest height. */
	FVector ResolveMuzzleLocation(AActor& Attacker, uint64 WeaponId) const;

	/** Where on a defender a bolt lands: the body centre. */
	static FVector ResolveHitPoint(const AActor& Defender);

	/** The scene component and socket an effect on Actor should attach to for a hardpoint name (weapon hardpoint, else mesh socket, else mesh). */
	USceneComponent* ResolveAttachPoint(AActor& Actor, const FString& Hardpoint, uint64 WeaponId, FName& OutSocket) const;

	const FSWGDataTableData* GetSwooshTable();

	UPROPERTY()
	TObjectPtr<USWGCombatSubsystem> Combat;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	/** Swing ribbons per attacker (one per trail edge), created on first swing. */
	UPROPERTY()
	TMap<int64, FSWGTrailSet> Trails;

	TArray<FProjectile> Projectiles;

	struct FTrailStop
	{
		TWeakObjectPtr<USWGTrailComponent> Trail;
		double EndTime = 0.0;
	};
	TArray<FTrailStop> TrailStops;

	TSharedPtr<FSWGDataTableData> SwooshTable;
	bool bSwooshTableLoadAttempted = false;
	TMap<FString, TSharedPtr<FSWGWeaponVisuals>> VisualsCache;

	FDelegateHandle ResolvedHandle;
};
