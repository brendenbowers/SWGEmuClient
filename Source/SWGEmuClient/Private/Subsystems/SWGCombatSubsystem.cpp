#include "Subsystems/SWGCombatSubsystem.h"

#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGTargetSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Components/SWGCombatStateComponent.h"
#include "Common/SWGPostureTypes.h"
#include "Common/SWGLocomotionResolver.h"
#include "Objects/SWGNetworkObjectInterface.h"

#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGCombatManagerReader.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGCrc32.h"

#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/ObjControllerMessageIn.h"
#include "Network/Messages/Zone/ChatSystemMessage.h"
#include "Network/Messages/Zone/Object/CombatActionIn.h"
#include "Network/Messages/Zone/Object/CombatSpamIn.h"
#include "Network/Messages/Zone/Object/CommandQueueRemoveIn.h"

#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWGCombat, Log, All);

static int32 GSWGLogObjController = 0;
static FAutoConsoleVariableRef CVarSWGLogObjController(
	TEXT("swg.LogObjController"),
	GSWGLogObjController,
	TEXT("Log every inbound ObjectController sub-message (sub-op, object, payload size). Use to tell 'the server sent nothing' from 'we mis-dispatched it'."),
	ECVF_Default);

namespace
{
	/** True for the postures a creature can't be attacked out of any more. */
	bool IsDownedPosture(uint8 Posture)
	{
		const ESWGPosture Value = static_cast<ESWGPosture>(Posture);
		return Value == ESWGPosture::Incapacitated || Value == ESWGPosture::Dead;
	}

	const TCHAR* StopReasonName(ESWGAttackStopReason Reason)
	{
		switch (Reason)
		{
			case ESWGAttackStopReason::Cancelled:    return TEXT("cancelled");
			case ESWGAttackStopReason::TargetGone:   return TEXT("target gone");
			case ESWGAttackStopReason::Rejected:     return TEXT("rejected");
			case ESWGAttackStopReason::Disconnected: return TEXT("disconnected");
			default:                                 return TEXT("unknown");
		}
	}
}

void USWGCombatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Network     = Cast<USWGNetworkSubsystem>(Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));
	ObjectGraph = Cast<USWGObjectGraphSubsystem>(Collection.InitializeDependency(USWGObjectGraphSubsystem::StaticClass()));
	Commands    = Cast<USWGCommandSubsystem>(Collection.InitializeDependency(USWGCommandSubsystem::StaticClass()));
	Targeting   = Cast<USWGTargetSubsystem>(Collection.InitializeDependency(USWGTargetSubsystem::StaticClass()));
	MeshGenerator = Cast<USWGMeshGeneratorSubsystem>(Collection.InitializeDependency(USWGMeshGeneratorSubsystem::StaticClass()));
	Tre         = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGCombatSubsystem::HandleMessageReceived);
	}

	if (ObjectGraph)
	{
		ObjectDestroyedHandle = ObjectGraph->OnObjectDestroyed.AddUObject(this, &USWGCombatSubsystem::HandleObjectDestroyed);
	}

	if (Commands)
	{
		CommandSentHandle = Commands->OnCommandSent.AddUObject(this, &USWGCombatSubsystem::HandleCommandSent);
	}
}

void USWGCombatSubsystem::HandleCommandSent(const FString& CommandName, uint32 ActionCount)
{
	// Peace only holds if we stop swinging. attemptPeace clears the defender
	// list and sets PEACE, but the next queued attack calls startCombat, which
	// clears PEACE again — so a running loop cancels the peace a second later.
	if (IsAttacking() && CommandName.Equals(PeaceCommandName, ESearchCase::IgnoreCase))
	{
		Stop(ESWGAttackStopReason::Cancelled, TEXT("peace"));
	}
}

void USWGCombatSubsystem::Deinitialize()
{
	if (Network && MessageHandle.IsValid())
	{
		Network->OnMessageReceived.Remove(MessageHandle);
		MessageHandle.Reset();
	}

	if (ObjectGraph && ObjectDestroyedHandle.IsValid())
	{
		ObjectGraph->OnObjectDestroyed.Remove(ObjectDestroyedHandle);
		ObjectDestroyedHandle.Reset();
	}

	if (Commands && CommandSentHandle.IsValid())
	{
		Commands->OnCommandSent.Remove(CommandSentHandle);
		CommandSentHandle.Reset();
	}

	AttackTargetId = 0;
	CooldownRemaining = 0.f;
	PendingActionCount = 0;
	PendingElapsed = 0.f;

	Network = nullptr;
	ObjectGraph = nullptr;
	Commands = nullptr;
	Targeting = nullptr;
	MeshGenerator = nullptr;
	Tre = nullptr;

	CombatManager.Reset();
	bCombatManagerLoadAttempted = false;
}

const FSWGCombatManagerData* USWGCombatSubsystem::GetCombatManager()
{
	if (bCombatManagerLoadAttempted)
	{
		return CombatManager.Get();
	}

	bCombatManagerLoadAttempted = true;

	if (!Tre)
	{
		return nullptr;
	}

	// Note the path: this file sits at the archive root, not under
	// datatables/, which is why it does not turn up in a datatable sweep.
	TSharedPtr<FSWGCombatManagerData> Loaded = MakeShared<FSWGCombatManagerData>();
	if (!FSWGCombatManagerReader::ReadCombatManager(Tre->CreateIffReader(TEXT("combat/combat_manager.iff")), *Loaded))
	{
		UE_LOG(LogSWGCombat, Warning, TEXT("could not read combat/combat_manager.iff — combat will play no animations"));
		return nullptr;
	}

	CombatManager = MoveTemp(Loaded);
	return CombatManager.Get();
}

FString USWGCombatSubsystem::ResolveWeaponStateName(int64 WeaponObjectId) const
{
	if (WeaponObjectId == 0 || !ObjectGraph || !Tre)
	{
		return FString();
	}

	const AActor* WeaponActor = ObjectGraph->FindActor(WeaponObjectId);
	const ISWGNetworkObjectInterface* NetworkObject = Cast<const ISWGNetworkObjectInterface>(WeaponActor);
	if (!NetworkObject)
	{
		// An unarmed creature's "weapon" is a server-side default object that
		// is never sent to us, so this is the ordinary case, not a failure.
		return FString();
	}

	const FString* TemplatePath = Tre->GetCrcToTemplatePathMap().Find(NetworkObject->GetObjectCrc());
	return TemplatePath ? SWGLocomotion::WeaponStateNameForTemplate(*TemplatePath) : FString();
}

void USWGCombatSubsystem::PlayCombatAnimations(const FSWGCombatActionAnimation& Animation, int64 AttackerId, int64 DefenderId, int64 WeaponId)
{
	if (!MeshGenerator || !ObjectGraph)
	{
		return;
	}

	if (!Animation.AttackerAction.IsEmpty())
	{
		if (AActor* Attacker = ObjectGraph->FindActor(AttackerId))
		{
			// The attacker's weapon decides which .ash subtree the action
			// resolves in; the defender's reaction is authored against its
			// own stance, so it uses whatever that creature is holding.
			MeshGenerator->PlayCombatAction(*Attacker, Animation.AttackerAction, ResolveWeaponStateName(WeaponId));
		}
	}

	if (!Animation.DefenderAction.IsEmpty())
	{
		if (AActor* Defender = ObjectGraph->FindActor(DefenderId))
		{
			int64 DefenderWeaponId = 0;
			if (const USWGCombatStateComponent* CombatState = ObjectGraph->FindComponent<USWGCombatStateComponent>(DefenderId))
			{
				DefenderWeaponId = CombatState->WeaponId;
			}

			MeshGenerator->PlayCombatAction(*Defender, Animation.DefenderAction, ResolveWeaponStateName(DefenderWeaponId));
		}
	}
}

TStatId USWGCombatSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGCombatSubsystem, STATGROUP_Tickables);
}

bool USWGCombatSubsystem::IsTickable() const
{
	// Nothing to do until a swing is actually in flight.
	return AttackTargetId != 0;
}

// ── Driving the attack ───────────────────────────────────────────────────────

bool USWGCombatSubsystem::Attack(int64 TargetId)
{
	if (TargetId == 0)
	{
		TargetId = Targeting ? Targeting->GetTargetId() : 0;
	}

	if (TargetId == 0)
	{
		UE_LOG(LogSWGCombat, Warning, TEXT("Attack: nothing targeted"));
		return false;
	}

	// Attacking selects, the way clicking would — free when already targeted,
	// and keeps the server honest when the attack came from a macro.
	if (Targeting)
	{
		Targeting->SetTarget(TargetId);
	}

	AttackTargetId = TargetId;
	CooldownRemaining = 0.f;
	PendingActionCount = 0;
	PendingElapsed = 0.f;

	if (!SendSwing())
	{
		AttackTargetId = 0;
		return false;
	}

	return true;
}

void USWGCombatSubsystem::StopAttacking()
{
	if (AttackTargetId != 0)
	{
		Stop(ESWGAttackStopReason::Cancelled, FString());
	}
}

bool USWGCombatSubsystem::ToggleAttack(int64 TargetId)
{
	if (IsAttacking())
	{
		StopAttacking();
		return false;
	}

	return Attack(TargetId);
}

bool USWGCombatSubsystem::SendSwing()
{
	if (!Commands)
	{
		return false;
	}

	const int32 ActionCount = Commands->SendCommand(AttackCommandName, AttackTargetId);
	if (ActionCount == 0)
	{
		return false;
	}

	PendingActionCount = static_cast<uint32>(ActionCount);
	PendingElapsed = 0.f;
	return true;
}

void USWGCombatSubsystem::Stop(ESWGAttackStopReason Reason, const FString& Detail)
{
	UE_LOG(LogSWGCombat, Log, TEXT("attack on %lld stopped: %s%s%s"),
		AttackTargetId, StopReasonName(Reason),
		Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);

	AttackTargetId = 0;
	CooldownRemaining = 0.f;
	PendingActionCount = 0;
	PendingElapsed = 0.f;

	OnAttackStopped.Broadcast(Reason, Detail);
}

bool USWGCombatSubsystem::IsTargetStillValid(FString& OutReason) const
{
	if (!ObjectGraph || AttackTargetId == 0)
	{
		OutReason = TEXT("no object graph");
		return false;
	}

	if (ObjectGraph->GetLocalPlayerObjectId() == 0)
	{
		OutReason = TEXT("no local player object");
		return false;
	}

	// Death arrives as an ordinary CREO base3 posture change, so this is the
	// same signal the animation side reads — no separate death message.
	const USWGCombatStateComponent* CombatState =
		ObjectGraph->FindComponent<USWGCombatStateComponent>(AttackTargetId);

	if (CombatState && CombatState->bHasBase3 && IsDownedPosture(CombatState->Posture))
	{
		OutReason = FString::Printf(TEXT("target is down (posture %u)"), CombatState->Posture);
		return false;
	}

	// Deliberately not tested: whether the target has a spawned actor. That is
	// a client-side condition saying nothing about validity, and treating it as
	// terminal killed the attack on its first tick. OnObjectDestroyed and the
	// server's INVALIDTARGET are the authoritative signals.

	OutReason.Reset();
	return true;
}

void USWGCombatSubsystem::Tick(float DeltaTime)
{
	if (AttackTargetId == 0)
	{
		return;
	}

	FString InvalidReason;
	if (!IsTargetStillValid(InvalidReason))
	{
		Stop(ESWGAttackStopReason::TargetGone, InvalidReason);
		return;
	}

	// Waiting on the server's verdict for the swing already sent.
	if (PendingActionCount != 0)
	{
		PendingElapsed += DeltaTime;
		if (PendingElapsed >= PendingTimeout)
		{
			UE_LOG(LogSWGCombat, Warning, TEXT("no CommandQueueRemove for action %u after %.0fs — swinging again"),
				PendingActionCount, PendingTimeout);
			PendingActionCount = 0;
			CooldownRemaining = 0.f;
		}
		return;
	}

	CooldownRemaining = FMath::Max(0.f, CooldownRemaining - DeltaTime);
	if (CooldownRemaining > 0.f)
	{
		return;
	}

	if (!SendSwing())
	{
		Stop(ESWGAttackStopReason::Disconnected, TEXT("could not send the command"));
	}
}

void USWGCombatSubsystem::HandleObjectDestroyed(int64 ObjectId)
{
	if (ObjectId == AttackTargetId)
	{
		Stop(ESWGAttackStopReason::TargetGone, FString());
	}
}

// ── Inbound ──────────────────────────────────────────────────────────────────

void USWGCombatSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg)
{
	if (!Msg)
	{
		return;
	}

	// Logged here for want of a chat system. Most system messages explain a
	// refused command, and several failures say why *only* here — their
	// CommandQueueRemove carries a zero error that reads as success.
	if (Msg->Opcode == static_cast<uint32>(ESWGMessageOp::ChatSystemMessage))
	{
		const FChatSystemMessage& Chat = *static_cast<const FChatSystemMessage*>(Msg.Get());
		if (!Chat.Message.IsEmpty())
		{
			UE_LOG(LogSWGCombat, Warning, TEXT("server says: %s"), *(Tre ? Tre->ResolveStringId(Chat.Message) : Chat.Message));
		}
		return;
	}

	if (Msg->Opcode != static_cast<uint32>(ESWGMessageOp::ObjControllerMessage))
	{
		return;
	}

	const FObjControllerMessageIn& Envelope = *static_cast<const FObjControllerMessageIn*>(Msg.Get());

	if (GSWGLogObjController != 0)
	{
		FString Detail;
		if (Envelope.GetSubOp() == ESWGObjControllerOp::CommandQueueRemove)
		{
			FSWGPacket Peek = Envelope.AsPayloadPacket();
			FCommandQueueRemoveIn Reply;
			if (Reply.Parse(Peek))
			{
				Detail = FString::Printf(TEXT(" count=%u timer=%.3f error=%u detail=%u '%s'"),
					Reply.ActionCount, Reply.Timer, Reply.Error, Reply.ErrorDetail,
					Commands ? *Commands->FindCommandName(Reply.ActionCount) : TEXT(""));
			}
		}

		UE_LOG(LogSWGCombat, Log, TEXT("ObjController sub-op 0x%X priority %u object %lld (%d payload bytes)%s"),
			Envelope.Type, Envelope.Priority, Envelope.ObjectId, Envelope.RawPayload.Num(), *Detail);
	}

	switch (Envelope.GetSubOp())
	{
		case ESWGObjControllerOp::CommandQueueRemove:
			HandleCommandQueueRemove(Envelope);
			break;
		case ESWGObjControllerOp::CombatAction:
			HandleCombatAction(Envelope);
			break;
		case ESWGObjControllerOp::CombatSpam:
			HandleCombatSpam(Envelope);
			break;
		default:
			// Every other sub-op belongs to someone else (DataTransform to the
			// object graph) or isn't decoded yet. Not an error.
			break;
	}
}

void USWGCombatSubsystem::HandleCommandQueueRemove(const FObjControllerMessageIn& Envelope)
{
	FSWGPacket Payload = Envelope.AsPayloadPacket();
	FCommandQueueRemoveIn Reply;
	if (!Reply.Parse(Payload))
	{
		UE_LOG(LogSWGCombat, Warning, TEXT("malformed CommandQueueRemove (%d payload bytes)"), Envelope.RawPayload.Num());
		return;
	}

	// Every command's reply lands here, not just our swing's. Reporting the
	// failures is the only feedback the player gets: the server explains
	// itself over system chat, which we do not decode yet, so an ability that
	// is out of range or needs the wrong weapon otherwise does nothing at all
	// with nothing said.
	if (!Reply.IsSuccess())
	{
		const FString Name = Commands ? Commands->FindCommandName(Reply.ActionCount) : FString();
		UE_LOG(LogSWGCombat, Warning, TEXT("server refused '%s': %s"),
			Name.IsEmpty() ? TEXT("(unknown command)") : *Name, *Reply.DescribeError());
	}

	if (PendingActionCount == 0 || Reply.ActionCount != PendingActionCount)
	{
		return;
	}

	PendingActionCount = 0;
	PendingElapsed = 0.f;

	if (Reply.IsSuccess())
	{
		// The server's own cooldown, so the loop matches weapon speed rather
		// than guessing at it. A zero timer still respects the floor.
		CooldownRemaining = FMath::Max(Reply.Timer, MinSwingInterval);
		return;
	}

	// Being out of range or briefly in the wrong posture is what happens
	// while chasing something, not a reason to drop the attack — those
	// retry, everything else gives up.
	const ESWGCommandError Error = Reply.GetError();
	const bool bTransient =
		Error == ESWGCommandError::TooFar ||
		Error == ESWGCommandError::InvalidLocomotion ||
		Error == ESWGCommandError::InvalidState;

	if (bTransient)
	{
		CooldownRemaining = RetryInterval;
		UE_LOG(LogSWGCombat, Log, TEXT("retrying the swing in %.1fs"), RetryInterval);
		return;
	}

	Stop(ESWGAttackStopReason::Rejected, Reply.DescribeError());
}

void USWGCombatSubsystem::HandleCombatAction(const FObjControllerMessageIn& Envelope)
{
	FSWGPacket Payload = Envelope.AsPayloadPacket();
	FCombatActionIn Action;
	if (!Action.Parse(Payload))
	{
		UE_LOG(LogSWGCombat, Warning, TEXT("malformed CombatAction (%d payload bytes)"), Envelope.RawPayload.Num());
		return;
	}

	const int64 LocalPlayerId = ObjectGraph ? ObjectGraph->GetLocalPlayerObjectId() : 0;

	// The wire carries only the hash of the name the server composed, so the
	// entry is found by hashing every key in combat_manager.iff at load and
	// looking the CRC up — there is no name to match on.
	const FSWGCombatManagerData* Manager = GetCombatManager();
	const FSWGCombatManagerEntry* Entry = Manager ? Manager->FindByCrc(Action.AnimationCrc) : nullptr;

	if (Manager && !Entry)
	{
		UE_LOG(LogSWGCombat, Warning, TEXT("no combat_manager entry for animation CRC %08X"), Action.AnimationCrc);
	}

	for (const FSWGCombatDefender& Defender : Action.Defenders)
	{
		if (Entry)
		{
			// Each defender resolves its own arm of the entry: the same swing
			// is a hit for one target and a dodge for another, and the two
			// play different reactions.
			if (const FSWGCombatActionAnimation* Animation = Entry->Resolve(Defender.Hit, Defender.Posture))
			{
				PlayCombatAnimations(*Animation, Action.AttackerId, Defender.ObjectId, Action.WeaponId);
			}
		}

		FSWGCombatEvent Event;
		Event.AttackerId    = Action.AttackerId;
		Event.DefenderId    = Defender.ObjectId;
		Event.WeaponId      = Action.WeaponId;
		Event.AnimationCrc  = static_cast<int64>(Action.AnimationCrc);
		Event.Hit           = Defender.Hit;
		Event.InitialDamage = Defender.InitialDamage;
		Event.bLandedOnUs   = LocalPlayerId != 0 && Defender.ObjectId == LocalPlayerId;

		UE_LOG(LogSWGCombat, Log, TEXT("CombatAction %lld -> %lld hit=%u dmg=%u anim=%08X"),
			Event.AttackerId, Event.DefenderId, Event.Hit, Event.InitialDamage, Action.AnimationCrc);

		OnCombatAction.Broadcast(Event);
	}
}

void USWGCombatSubsystem::HandleCombatSpam(const FObjControllerMessageIn& Envelope)
{
	FSWGPacket Payload = Envelope.AsPayloadPacket();
	FCombatSpamIn Spam;
	if (!Spam.Parse(Payload))
	{
		UE_LOG(LogSWGCombat, Warning, TEXT("malformed CombatSpam (%d payload bytes)"), Envelope.RawPayload.Num());
		return;
	}

	const FString StringId = Spam.GetStringId();

	UE_LOG(LogSWGCombat, Log, TEXT("CombatSpam %s%s damage=%d"),
		StringId.IsEmpty() ? TEXT("") : *StringId,
		Spam.CustomText.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("\"%s\""), *Spam.CustomText),
		Spam.Damage);

	OnCombatSpam.Broadcast(StringId.IsEmpty() ? Spam.CustomText : (Tre ? Tre->ResolveStringId(StringId) : StringId), Spam.Damage, Spam.Color);
}

void USWGCombatSubsystem::DumpCombatAnimationChain(const FString& ServerAnimationName, uint8 HitResult, uint8 DefenderPosture)
{
	const uint32 Crc = FSWGCrc32::HashString(ServerAnimationName);
	UE_LOG(LogSWGCombat, Warning, TEXT("=== %s (crc %08X) hit=%u defenderPosture=%u"), *ServerAnimationName, Crc, HitResult, DefenderPosture);

	const FSWGCombatManagerData* Manager = GetCombatManager();
	if (!Manager)
	{
		UE_LOG(LogSWGCombat, Warning, TEXT("  combat_manager.iff unavailable"));
		return;
	}

	UE_LOG(LogSWGCombat, Warning, TEXT("  combat_manager: %d entries loaded"), Manager->Entries.Num());

	const FSWGCombatManagerEntry* Entry = Manager->FindByCrc(Crc);
	if (!Entry)
	{
		UE_LOG(LogSWGCombat, Warning, TEXT("  no entry for that name — check the spelling, the server appends a variant index (e.g. _0)"));
		return;
	}

	const FSWGCombatActionAnimation* Animation = Entry->Resolve(HitResult, DefenderPosture);
	if (!Animation)
	{
		UE_LOG(LogSWGCombat, Warning, TEXT("  entry '%s' has no arm for that hit/posture"), *Entry->Key);
		return;
	}

	UE_LOG(LogSWGCombat, Warning, TEXT("  script='%s' attacker='%s' defender='%s'"),
		*Animation->PlaybackScript, *Animation->AttackerAction, *Animation->DefenderAction);

	if (!ObjectGraph || !MeshGenerator)
	{
		return;
	}

	const int64 PlayerId = ObjectGraph->GetLocalPlayerObjectId();
	const int64 TargetId = Targeting ? Targeting->GetTargetId() : 0;

	int64 PlayerWeaponId = 0;
	if (const USWGCombatStateComponent* CombatState = ObjectGraph->FindComponent<USWGCombatStateComponent>(PlayerId))
	{
		PlayerWeaponId = CombatState->WeaponId;
	}

	if (AActor* Player = ObjectGraph->FindActor(PlayerId))
	{
		FString Trace;
		MeshGenerator->ResolveCombatActionClip(*Player, Animation->AttackerAction, ResolveWeaponStateName(PlayerWeaponId), &Trace);
		UE_LOG(LogSWGCombat, Warning, TEXT("  attacker (local player %lld): %s"), PlayerId, *Trace);
	}
	else
	{
		UE_LOG(LogSWGCombat, Warning, TEXT("  attacker: no local player actor"));
	}

	if (AActor* Target = TargetId != 0 ? ObjectGraph->FindActor(TargetId) : nullptr)
	{
		int64 TargetWeaponId = 0;
		if (const USWGCombatStateComponent* CombatState = ObjectGraph->FindComponent<USWGCombatStateComponent>(TargetId))
		{
			TargetWeaponId = CombatState->WeaponId;
		}

		FString Trace;
		MeshGenerator->ResolveCombatActionClip(*Target, Animation->DefenderAction, ResolveWeaponStateName(TargetWeaponId), &Trace);
		UE_LOG(LogSWGCombat, Warning, TEXT("  defender (target %lld): %s"), TargetId, *Trace);
	}
	else
	{
		UE_LOG(LogSWGCombat, Warning, TEXT("  defender: nothing targeted"));
	}
}

// ── Console ──────────────────────────────────────────────────────────────────

#if !UE_BUILD_SHIPPING

namespace
{
	// Resolved at invocation, not captured: a console command outlives the
	// subsystem, so a captured pointer goes stale next session.
	USWGCombatSubsystem* FindLiveCombatSubsystem()
	{
		for (TObjectIterator<USWGCombatSubsystem> It; It; ++It)
		{
			if (IsValid(*It) && It->GetGameInstance())
			{
				return *It;
			}
		}
		return nullptr;
	}
}

static FAutoConsoleCommand SWGAttackCmd(
	TEXT("swg.Attack"),
	TEXT("swg.Attack [objectId] — attacks the given object, or the current target when no id is given. Repeats until the target drops or swg.StopAttack."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			USWGCombatSubsystem* Combat = FindLiveCombatSubsystem();
			if (!Combat)
			{
				UE_LOG(LogSWGCombat, Warning, TEXT("swg.Attack: no live combat subsystem — not in a session yet"));
				return;
			}

			const int64 TargetId = Args.Num() >= 1 ? FCString::Atoi64(*Args[0]) : 0;

			if (Combat->Attack(TargetId))
			{
				UE_LOG(LogSWGCombat, Warning, TEXT("swg.Attack: attacking %lld"), Combat->GetAttackTargetId());
			}
			else
			{
				UE_LOG(LogSWGCombat, Warning, TEXT("swg.Attack: could not start an attack — target something first"));
			}
		}));

static FAutoConsoleCommand SWGDumpCombatAnimCmd(
	TEXT("swg.DumpCombatAnim"),
	TEXT("swg.DumpCombatAnim <serverAnimName> [hitResult] [defenderPosture] — walks the whole animation chain for a CombatAction name (e.g. attack_mid_center_light_0) and logs every hop, without needing a live fight."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogSWGCombat, Warning, TEXT("Usage: swg.DumpCombatAnim <serverAnimName> [hitResult] [defenderPosture]"));
				return;
			}

			USWGCombatSubsystem* Combat = FindLiveCombatSubsystem();
			if (!Combat)
			{
				UE_LOG(LogSWGCombat, Warning, TEXT("swg.DumpCombatAnim: no live combat subsystem"));
				return;
			}

			const FString AnimName = Args[0];
			const uint8 HitResult = Args.Num() >= 2 ? (uint8)FCString::Atoi(*Args[1]) : (uint8)ESWGCombatHit::Hit;
			const uint8 Posture = Args.Num() >= 3 ? (uint8)FCString::Atoi(*Args[2]) : (uint8)ESWGPosture::Upright;

			Combat->DumpCombatAnimationChain(AnimName, HitResult, Posture);
		}));

static FAutoConsoleCommand SWGStopAttackCmd(
	TEXT("swg.StopAttack"),
	TEXT("swg.StopAttack — stops the repeating attack started by swg.Attack."),
	FConsoleCommandDelegate::CreateLambda([]()
		{
			if (USWGCombatSubsystem* Combat = FindLiveCombatSubsystem())
			{
				Combat->StopAttacking();
			}
		}));

#endif // !UE_BUILD_SHIPPING
