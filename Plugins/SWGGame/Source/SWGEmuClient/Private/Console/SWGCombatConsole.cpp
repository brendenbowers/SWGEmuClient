#include "Subsystems/SWGCombatSubsystem.h"
#include "Subsystems/SWGCombatAudioSubsystem.h"
#include "Subsystems/SWGCombatVisualsSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Components/SWGEquipmentComponent.h"
#include "Objects/Creature/SWGCreature.h"
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

static FAutoConsoleCommand SWGCombatAudioTestCmd(
	TEXT("swg.Audio.CombatTest"),
	TEXT("swg.Audio.CombatTest <weaponTemplate> [hitResult] — plays a weapon's fire or swing sound on the local player, and the impact on the target if any. hitResult is an ESWGCombatHit (default 1, hit; 7 ricochet). e.g. object/weapon/ranged/pistol/shared_pistol_dl44.iff"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogSWGCombat, Warning, TEXT("Usage: swg.Audio.CombatTest <weaponTemplate> [hitResult]"));
				return;
			}

			for (TObjectIterator<USWGCombatAudioSubsystem> It; It; ++It)
			{
				if (IsValid(*It) && It->GetGameInstance())
				{
					FString Report;
					const uint8 HitResult = Args.Num() >= 2 ? (uint8)FCString::Atoi(*Args[1]) : (uint8)ESWGCombatHit::Hit;
					It->PlayTestSwing(Args[0], HitResult, Report);
					UE_LOG(LogSWGCombat, Warning, TEXT("swg.Audio.CombatTest: %s"), *Report);
					return;
				}
			}
			UE_LOG(LogSWGCombat, Warning, TEXT("swg.Audio.CombatTest: no live combat audio subsystem"));
		}));

static FAutoConsoleCommand SWGFxCombatTestCmd(
	TEXT("swg.Fx.CombatTest"),
	TEXT("swg.Fx.CombatTest <weaponTemplate> [hitResult] — puts the weapon in the local player's hand and, once its mesh is up, fires it (muzzle flash, bolt flight, impact) at the current target or a point ahead, or swings it for its trail. hitResult is an ESWGCombatHit (default 1 hit; 0 miss flies past; 7 ricochet). e.g. object/weapon/ranged/pistol/shared_pistol_dl44.iff"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogSWGCombat, Warning, TEXT("Usage: swg.Fx.CombatTest <weaponTemplate> [hitResult]"));
				return;
			}

			for (TObjectIterator<USWGCombatVisualsSubsystem> It; It; ++It)
			{
				if (IsValid(*It) && It->GetGameInstance())
				{
					FString Report;
					const uint8 HitResult = Args.Num() >= 2 ? (uint8)FCString::Atoi(*Args[1]) : (uint8)ESWGCombatHit::Hit;
					It->PlayTestShot(Args[0], HitResult, Report);
					UE_LOG(LogSWGCombat, Warning, TEXT("swg.Fx.CombatTest: %s"), *Report);
					return;
				}
			}
			UE_LOG(LogSWGCombat, Warning, TEXT("swg.Fx.CombatTest: no live combat visuals subsystem"));
		}));

static FAutoConsoleCommand SWGFxSaberCmd(
	TEXT("swg.Fx.Saber"),
	TEXT("swg.Fx.Saber [lsb] [weaponTemplate] — ignites a lightsaber in the local player's hand: a .lsb (a bare name like wp_mle_lightsaber_blade_obi works), plus the weapon template for its blade colour and ignite sounds. No arguments retracts and removes it."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			for (TObjectIterator<USWGObjectGraphSubsystem> It; It; ++It)
			{
				if (!IsValid(*It) || !It->GetGameInstance())
				{
					continue;
				}

				const ASWGCreature* Player = Cast<ASWGCreature>(It->FindActor(It->GetLocalPlayerObjectId()));
				if (!Player || !Player->EquipmentComponent)
				{
					UE_LOG(LogSWGCombat, Warning, TEXT("swg.Fx.Saber: no local player"));
					return;
				}

				FString LsbPath = Args.Num() >= 1 ? Args[0] : FString();
				if (!LsbPath.IsEmpty() && !LsbPath.Contains(TEXT("/")))
				{
					LsbPath = TEXT("appearance/") + LsbPath;
				}
				if (!LsbPath.IsEmpty() && !LsbPath.EndsWith(TEXT(".lsb")))
				{
					LsbPath += TEXT(".lsb");
				}
				Player->EquipmentComponent->AttachDebugItem(LsbPath, Args.Num() >= 2 ? Args[1] : FString());
				UE_LOG(LogSWGCombat, Warning, TEXT("swg.Fx.Saber: %s"), LsbPath.IsEmpty() ? TEXT("retracted") : *LsbPath);
				return;
			}
			UE_LOG(LogSWGCombat, Warning, TEXT("swg.Fx.Saber: not in a session yet"));
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
