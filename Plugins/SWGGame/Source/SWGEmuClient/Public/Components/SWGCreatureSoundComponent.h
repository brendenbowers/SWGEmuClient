#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/SWGAnimEventNotify.h"
#include "TRE/SWGClientDataFileReader.h"
#include "Playback/SWGSoundHandle.h"
#include "SWGCreatureSoundComponent.generated.h"

class ASWGCell;
class USkeletalMeshComponent;
class USWGAudioSubsystem;
class USWGTreSubsystem;

/** The moments of a swing its clip marks: the shot leaves (fireN), the weapon swings (swing_*) or lands (hit_*). */
enum class ESWGAttackPhase : uint8 { Fire, Swing, Hit };

/** The sounds one swing plays on its attacker, armed by USWGCombatAudioSubsystem and fired by the clip's own events. */
struct FSWGAttackSounds
{
	/** Ranged: clienteffect played on each fireN event. */
	FString FireEffect;

	/** Melee: played on swing_* / uswing_* events. */
	FString SwingEffect;

	/** Melee: played on hit_* / uhit_* events, only when the swing landed. */
	FString HitEffect;

	bool bLanded = false;

	/** A ranged attack (fireN events), for the fallback when no clip event arrives. */
	bool bRanged = false;

	/** The wielded weapon's object id, to find its muzzle for the fire effect. */
	int64 WeaponId = 0;

	/** Called when a phase happens (sounds already started), for the visuals that go with it. */
	TFunction<void(ESWGAttackPhase)> OnPhase;
};

/**
 * Turns a creature's animation events and its .cdf sound hooks into audio. The notifies baked
 * onto generated clips (USWGAnimEventNotify) land in OnAnimEvent: event_X maps to the cdf's
 * CSND / CEFT / EVNT entry X (footstep prefers footstep_<surface>), sound_<name> plays
 * sound/<name>.snd directly, and the ASND loop (breathing) follows the mesh while audible.
 * fireN / swing_* / hit_* (weapon clips) play what ArmAttackSounds armed for the current swing.
 */
UCLASS(ClassGroup = "SWGEmu", meta = (BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGCreatureSoundComponent : public UActorComponent, public ISWGAnimEventReceiver
{
	GENERATED_BODY()

public:
	USWGCreatureSoundComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Hands over the creature's parsed .cdf and starts the ASND loops. Game thread. */
	void SetClientData(const FSWGClientDataFile& InClientData);

	// ISWGAnimEventReceiver
	virtual void OnAnimEvent(FName EventName, USkeletalMeshComponent* MeshComp) override;

	/** Fires an animation event by name ("event_footstep") or bare cdf name ("footstep"). True if a sound started. */
	bool FireEvent(FName EventName);

	/** Footstep surface under this creature right now: interior.iff for a POB cell, otherwise the terrain shader. May be empty. */
	FString ResolveSurface();

	/**
	 * Every cdf target an event could play, best match first (the caller picks one at random).
	 * "event_X" -> X, with footstep first trying footstep_<Surface>; other names are tried as-is.
	 * sound_<name> and fireN are not cdf lookups and yield nothing here.
	 */
	static void CollectEventTargets(const FSWGClientDataFile& Data, const FString& EventName, const FString& Surface, TArray<FString>& OutTargets);

	/** Candidate .snd paths for an animation "sound_<name>" message, in preference order (sound/<name>.snd, then the cr_ prefix retail uses). */
	static void CollectDirectSoundPaths(const FString& EventName, TArray<FString>& OutPaths);

	/** Whether the name is a weapon-fire message ("fire1", "fire_9", "fire2_2"). */
	static bool IsFireEvent(const FString& EventName);

	/** Melee clip messages: "swing_whp1", "uswing_hold_l" (unarmed) and "hit_whp1", "uhit_rankle". */
	static bool IsSwingEvent(const FString& EventName);
	static bool IsMeleeHitEvent(const FString& EventName);

	/** Arms the next swing; its clip events (or, failing those, a short fallback timer) play the sounds. */
	void ArmAttackSounds(const FSWGAttackSounds& Sounds);

	/** Plays a clienteffect or .snd on this creature (or on AttachTo, a weapon muzzle say) if it is audible from the listener. False when nothing started. */
	bool PlayCombatEffect(const FString& Target, USceneComponent* AttachTo = nullptr);

	/** True for a path that names a clienteffect rather than a .snd. */
	static bool IsEffectPath(const FString& Target);

	const FSWGClientDataFile& GetClientData() const { return ClientData; }

private:
	bool PlayTarget(const FString& Target, USceneComponent* AttachTo);
	bool IsTargetAudible(const FString& Target, const FVector& ListenerLocation, const FVector& SourceLocation) const;
	bool TryGetListenerLocation(FVector& OutLocation) const;
	USkeletalMeshComponent* GetMeshComponent() const;

	/** The armed weapon's muzzle hardpoint, if it has one. */
	USceneComponent* FindMuzzle() const;

	/** Starts/stops the ASND loops as the listener comes into and out of range. */
	void UpdateAttachedLoops();

	void StopAttachedLoops();

	FString LookupInteriorSurface(ASWGCell* Cell);

	USWGAudioSubsystem* GetAudio() const;
	USWGTreSubsystem* GetTre() const;

	FSWGClientDataFile ClientData;
	bool bHasClientData = false;

	TMap<FName, double> LastPlayedTime;

	void PlayArmedAttack(ESWGAttackPhase Phase, FName EventName);
	void PlayAttackFallback();

	FSWGAttackSounds ArmedAttack;
	double ArmedAttackExpiry = 0.0;
	bool bAttackArmed = false;
	bool bArmedAttackPlayed = false;
	FTimerHandle AttackFallbackTimer;

	struct FAttachedLoop
	{
		FString Path;
		FSWGSoundHandle Handle;
		/** Started and not yet stopped for range; a loop that ends on its own is not restarted. */
		bool bActive = false;
	};
	TArray<FAttachedLoop> AttachedLoops;
	FTimerHandle LoopTimer;

	TWeakObjectPtr<ASWGCell> InteriorCell;
	FString InteriorSurface;
};
