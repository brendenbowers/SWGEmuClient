#include "Subsystems/SWGCombatSubsystem.h"
#include "SWGLogCategories.h"
#include "Common/SWGPostureTypes.h"
#include "Network/Messages/Zone/Object/CombatActionIn.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"


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
