#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGCombatAudioSubsystem.generated.h"

class AActor;
class USWGAudioSubsystem;
class USWGCombatSubsystem;
class USWGCombatVisualsSubsystem;
class USWGCreatureSoundComponent;
class USWGObjectGraphSubsystem;
class USWGTreSubsystem;
struct FCombatActionIn;
struct FSWGCombatActionAnimation;
struct FSWGDataTableData;
struct FSWGResolvedCombatDefender;

/**
 * Where a weapon's combat sounds come from, all data-driven:
 *  - template SWOT weaponEffect + weaponEffectIndex select a row of datatables/weapon/weapon.iff,
 *    whose clienteffect columns hold the muzzle (Fire), impact (Hit/Miss per surface) and Ricochet
 *    effects. Only the sounds (PSND) of those effects are played.
 *  - melee weapons (and a creature's own cdf when unarmed) name swing / hit clienteffects in
 *    their .cdf CEFT events; the swing_* / hit_* clip messages trigger them.
 */
struct FSWGWeaponSoundSet
{
	FString WeaponTemplate;
	FString EffectId;
	int32 EffectIndex = INDEX_NONE;

	/** "Fire Client Effect": muzzle flash and shot. */
	FString FireEffect;
	/** "Projectile Appearance Template": the bolt .prt that flies from the muzzle (empty for melee and some launchers). */
	FString ProjectileEffect;
	/** "Hit (Creature) Client Effect": the bolt landing on a creature. */
	FString HitCreatureEffect;
	/** "Miss (Hit Terrain) Client Effect": a bolt that missed, landing past the target. */
	FString MissTerrainEffect;
	/** "Ricochet Client Effect". */
	FString RicochetEffect;

	/** .cdf CEFT "swing" (the whoosh) and "hit" (the blow landing). */
	FString SwingEffect;
	FString HitEffect;
};

/**
 * Plays weapon fire, swing, impact and playback-script sounds for every CombatAction in sight.
 * The attacker's clip messages (fireN, swing_*, hit_*) time the weapon sounds through
 * USWGCreatureSoundComponent, which also calls USWGCombatVisualsSubsystem for the bolt (and its impact on arrival) or swing ribbon.
 */
UCLASS()
class SWGEMUCLIENT_API USWGCombatAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Resolves a weapon template's sounds against the given weapon.iff table. False when the
	 * template yields nothing at all. Pure data: the subsystem caches around it.
	 */
	static bool ResolveWeaponSounds(USWGTreSubsystem& Tre, const FSWGDataTableData& WeaponTable, const FString& WeaponTemplate, FSWGWeaponSoundSet& OutSounds);

	/** Which weapon.iff effect plays when a projectile ends the way HitResult (ESWGCombatHit) says. Empty for none. */
	static const FString& PickImpactEffect(const FSWGWeaponSoundSet& Sounds, uint8 HitResult);

	/** The cached sounds and effects of a weapon template, or null when it resolves to none. */
	const FSWGWeaponSoundSet* FindWeaponSounds(const FString& WeaponTemplate) { return GetWeaponSounds(WeaponTemplate); }

	/** Plays what a swing with the weapon would sound like on the local player. Fills OutReport. */
	bool PlayTestSwing(const FString& WeaponTemplate, uint8 HitResult, FString& OutReport);

	/** Metres per second a projectile travels (the playback scripts' LEVP default). */
	static constexpr float ProjectileSpeedMetres = 55.f;

private:
	void HandleCombatActionResolved(const FCombatActionIn& Action, const TArray<FSWGResolvedCombatDefender>& Defenders);

	/** Resolves and caches a weapon template's sounds. Null when it has none. */
	const FSWGWeaponSoundSet* GetWeaponSounds(const FString& WeaponTemplate);

	/** The BDNS / LDNS / EDNS (SNDB / SNDL / SNDE) sounds of a playback script entry, on the attacker. */
	void PlayScriptSounds(const FSWGCombatActionAnimation& Animation, AActor& Attacker, USWGCreatureSoundComponent* AttackerSound);

	/** Plays an effect on a creature after a delay, if it still exists then. */
	static void PlayCombatEffectLater(USWGCreatureSoundComponent* Sound, const FString& Effect, float DelaySeconds);

	UPROPERTY()
	TObjectPtr<USWGCombatSubsystem> Combat;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGCombatVisualsSubsystem> Visuals;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TObjectPtr<USWGAudioSubsystem> Audio;

	TSharedPtr<FSWGDataTableData> WeaponTable;
	bool bWeaponTableLoadAttempted = false;
	TMap<FString, TSharedPtr<FSWGWeaponSoundSet>> WeaponSoundCache;

	FDelegateHandle ResolvedHandle;
};
