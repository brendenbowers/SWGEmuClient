#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "SWGCombatSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGObjectGraphSubsystem;
class USWGCommandSubsystem;
class USWGTargetSubsystem;
class USWGMeshGeneratorSubsystem;
class USWGTreSubsystem;
struct FSWGCombatManagerData;
struct FSWGCombatActionAnimation;
struct FSWGNetMessage;
struct FObjControllerMessageIn;
struct FCombatActionIn;
struct FCombatSpamIn;
struct FCommandQueueRemoveIn;

/** Why an auto-attack run ended, for the UI and for logs. */
UENUM(BlueprintType)
enum class ESWGAttackStopReason : uint8
{
	/** StopAttacking() was called, or a new attack replaced this one. */
	Cancelled,
	/** The target died, went away, or stopped being our target. */
	TargetGone,
	/** The server refused the command in a way that retrying will not fix. */
	Rejected,
	/** We lost the zone connection or the local player object. */
	Disconnected,
};

/** One swing, flattened for Blueprint. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGCombatEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Combat")
	int64 AttackerId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Combat")
	int64 DefenderId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Combat")
	int64 WeaponId = 0;

	/** FSWGCrc32 hash of the server's logical animation name. Unresolved — see FCombatActionIn. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Combat")
	int64 AnimationCrc = 0;

	/** An ESWGCombatHit: 0 miss, 1 hit, 2 block, 3 dodge, 4 counter, 5 ricochet. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Combat")
	uint8 Hit = 0;

	/** Pre-mitigation damage, byte-clamped on the wire. A weight hint, not a real number. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Combat")
	uint8 InitialDamage = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Combat")
	bool bLandedOnUs = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSWGOnCombatEvent, const FSWGCombatEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSWGOnCombatSpam, const FString&, Text, int32, Damage, uint8, Color);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSWGOnAttackStopped, ESWGAttackStopReason, Reason, const FString&, Detail);

/**
 * Drives attacking: sends the attack command, keeps it repeating while the
 * target is alive, and decodes the combat traffic the server sends back.
 *
 * SWG's auto-attack lives on the client: Core3 runs a queued command once and
 * drains its queue, so sustained combat is the client re-sending. The repeat
 * loop is timed off CommandQueueRemove's cooldown, not a guessed interval.
 */
UCLASS()
class SWGEMUCLIENT_API USWGCombatSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/**
	 * Starts attacking TargetId, or whatever is currently targeted when it is
	 * 0. Keeps swinging until the target dies, StopAttacking() is called, or
	 * the server rejects the command for a reason retrying will not fix.
	 * Returns false if there was nothing to attack or nothing to send through.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Combat")
	bool Attack(int64 TargetId = 0);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Combat")
	void StopAttacking();

	/** Attacks if idle, stops if already going — what a single "attack" key should do. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Combat")
	bool ToggleAttack(int64 TargetId = 0);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Combat")
	bool IsAttacking() const { return AttackTargetId != 0; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Combat")
	int64 GetAttackTargetId() const { return AttackTargetId; }

	/** Seconds until the next swing goes out. 0 when idle or ready. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Combat")
	float GetCooldownRemaining() const { return CooldownRemaining; }

	/** Every swing we can see, ours and other people's. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Combat")
	FSWGOnCombatEvent OnCombatAction;

	/** One combat log line. Text is the "@file:name" reference resolved through the .stf tables, or the line's own custom text. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Combat")
	FSWGOnCombatSpam OnCombatSpam;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Combat")
	FSWGOnAttackStopped OnAttackStopped;

	/** The command name sent per swing. "attack" is the one combat command every player may use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Combat")
	FString AttackCommandName = TEXT("attack");

	/** Sending this cancels the repeat loop — otherwise the next swing puts the player straight back into combat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Combat")
	FString PeaceCommandName = TEXT("peace");

	/**
	 * combat/combat_manager.iff, parsed on first use. Null if unreadable, in
	 * which case combat still works and simply plays no animation.
	 */
	const FSWGCombatManagerData* GetCombatManager();

	/** The .ash weapon subtree a creature's currently-wielded weapon selects. Empty for unarmed or unknown. */
	FString ResolveWeaponStateName(int64 WeaponObjectId) const;

	/**
	 * Logs every hop of the animation chain for one server animation name,
	 * against the local player and its target. Backs swg.DumpCombatAnim, which
	 * needs no live fight — just something targeted.
	 */
	void DumpCombatAnimationChain(const FString& ServerAnimationName, uint8 HitResult, uint8 DefenderPosture);

private:
	/** Plays one swing's attacker and defender animations. No-op where either doesn't resolve. */
	void PlayCombatAnimations(const FSWGCombatActionAnimation& Animation, int64 AttackerId, int64 DefenderId, int64 WeaponId);

	void HandleCommandSent(const FString& CommandName, uint32 ActionCount);
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg);
	void HandleCommandQueueRemove(const FObjControllerMessageIn& Envelope);
	void HandleCombatAction(const FObjControllerMessageIn& Envelope);
	void HandleCombatSpam(const FObjControllerMessageIn& Envelope);

	/** Sends one swing and arms the wait for its reply. False if it couldn't be sent. */
	bool SendSwing();

	void Stop(ESWGAttackStopReason Reason, const FString& Detail);

	/** True while the target still exists and is worth swinging at. OutReason names the failing check, for the stop log. */
	bool IsTargetStillValid(FString& OutReason) const;

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGCommandSubsystem> Commands;

	UPROPERTY()
	TObjectPtr<USWGTargetSubsystem> Targeting;

	UPROPERTY()
	TObjectPtr<USWGMeshGeneratorSubsystem> MeshGenerator;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	TSharedPtr<FSWGCombatManagerData> CombatManager;

	/** Set once the load has been attempted, so a missing file isn't re-read every swing. */
	bool bCombatManagerLoadAttempted = false;

	/** Who we are swinging at. 0 means not attacking. */
	int64 AttackTargetId = 0;

	/** Seconds before the next swing. Set from the server's own cooldown. */
	float CooldownRemaining = 0.f;

	/** ActionCount of the swing we are waiting on a reply for. 0 when not waiting. */
	uint32 PendingActionCount = 0;

	float PendingElapsed = 0.f;

	/** Wait for a CommandQueueRemove before swinging again anyway. attack has
	 *  addToCombatQueue set, so a reply is expected — this covers a lost one. */
	static constexpr float PendingTimeout = 5.f;

	/** Floor on the gap between swings, so a zero cooldown can't flood the wire.
	 *  The server's own cooldown is authoritative and normally well above it. */
	static constexpr float MinSwingInterval = 0.5f;

	/** Retry delay after a transient rejection — out of range, wrong posture. */
	static constexpr float RetryInterval = 1.f;

	FDelegateHandle MessageHandle;
	FDelegateHandle ObjectDestroyedHandle;
	FDelegateHandle CommandSentHandle;

	void HandleObjectDestroyed(int64 ObjectId);
};
