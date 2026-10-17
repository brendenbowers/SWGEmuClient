#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Animation/SWGCharacterAnimInstance.h"
#include "Objects/Creature/SWGCreature.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

#if !UE_BUILD_SHIPPING

namespace
{
	struct FSWGAnimConsoleSession
	{
		USWGMeshGeneratorSubsystem* MeshGenerator = nullptr;
		USWGObjectGraphSubsystem* ObjectGraph = nullptr;

		/** The actor the command is aimed at: ObjectId when given, else the local player. */
		AActor* ResolveActor(int64 ObjectId) const
		{
			return ObjectGraph ? ObjectGraph->FindActor(ObjectId != 0 ? ObjectId : ObjectGraph->GetLocalPlayerObjectId()) : nullptr;
		}
	};

	// Resolved at invocation, not captured: a console command outlives the
	// subsystems, so a captured pointer goes stale next session.
	bool FindSession(FSWGAnimConsoleSession& OutSession)
	{
		for (TObjectIterator<USWGMeshGeneratorSubsystem> It; It; ++It)
		{
			if (IsValid(*It) && It->GetGameInstance())
			{
				OutSession.MeshGenerator = *It;
				OutSession.ObjectGraph = It->GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>();
				return OutSession.ObjectGraph != nullptr;
			}
		}
		return false;
	}

	bool ParseOnOff(const TArray<FString>& Args, bool& bOutValue)
	{
		if (Args.Num() < 1)
		{
			return false;
		}
		bOutValue = Args[0].Equals(TEXT("on"), ESearchCase::IgnoreCase) || Args[0] == TEXT("1") || Args[0].Equals(TEXT("true"), ESearchCase::IgnoreCase);
		return bOutValue || Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase) || Args[0] == TEXT("0") || Args[0].Equals(TEXT("false"), ESearchCase::IgnoreCase);
	}
}

static FAutoConsoleCommand SWGAnimLookAtCmd(
	TEXT("swg.Anim.LookAt"),
	TEXT("swg.Anim.LookAt <on|off> — heads turn toward the creature's target (retail LookAtTransformModifier, yaw clamp 70 deg, 600 deg/s). Off shows the plain pose."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			bool bEnabled = false;
			if (!ParseOnOff(Args, bEnabled))
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.Anim.LookAt: head look-at is %s. Usage: swg.Anim.LookAt <on|off>"), USWGCharacterAnimInstance::bLookAtEnabled ? TEXT("on") : TEXT("off"));
				return;
			}
			USWGCharacterAnimInstance::bLookAtEnabled = bEnabled;
			UE_LOG(LogTemp, Warning, TEXT("swg.Anim.LookAt: %s"), bEnabled ? TEXT("on") : TEXT("off"));
		}));

static FAutoConsoleCommand SWGAnimAimCmd(
	TEXT("swg.Anim.Aim"),
	TEXT("swg.Anim.Aim <on|off> — in combat the upper body pitches toward the target's elevation (retail TargetPitchTransformModifier, clamp 45 deg)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			bool bEnabled = false;
			if (!ParseOnOff(Args, bEnabled))
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.Anim.Aim: aim pitch is %s. Usage: swg.Anim.Aim <on|off>"), USWGCharacterAnimInstance::bAimEnabled ? TEXT("on") : TEXT("off"));
				return;
			}
			USWGCharacterAnimInstance::bAimEnabled = bEnabled;
			UE_LOG(LogTemp, Warning, TEXT("swg.Anim.Aim: %s"), bEnabled ? TEXT("on") : TEXT("off"));
		}));

static FAutoConsoleCommand SWGAnimOverlayCmd(
	TEXT("swg.Anim.Overlay"),
	TEXT("swg.Anim.Overlay <action> [weaponNode] [objectId] — plays an .ash action on the local player (or objectId) on its retail track: moving MVAC variants and masked clips over locomotion, useAddTrack clips additive. e.g. swg.Anim.Overlay attack_mid_center_0 or swg.Anim.Overlay add_face_blink"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("Usage: swg.Anim.Overlay <action> [weaponNode] [objectId]"));
				return;
			}

			FSWGAnimConsoleSession Session;
			if (!FindSession(Session))
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.Anim.Overlay: not in a session yet"));
				return;
			}

			AActor* Actor = Session.ResolveActor(Args.Num() >= 3 ? FCString::Atoi64(*Args[2]) : 0);
			if (!Actor)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.Anim.Overlay: no such actor"));
				return;
			}

			FString Trace;
			const bool bStarted = Session.MeshGenerator->PlayAnimAction(*Actor, Args[0], Args.Num() >= 2 ? Args[1] : FString(), &Trace);
			UE_LOG(LogTemp, Warning, TEXT("swg.Anim.Overlay: %s %s"), bStarted ? TEXT("playing") : TEXT("not played:"), *Trace);
		}));

static FAutoConsoleCommand SWGAnimDumpCmd(
	TEXT("swg.Anim.Dump"),
	TEXT("swg.Anim.Dump [objectId] — logs the loop kind, blend space, active tracks and look-at/aim state of the local player (or objectId)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FSWGAnimConsoleSession Session;
			if (!FindSession(Session))
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.Anim.Dump: not in a session yet"));
				return;
			}

			AActor* Actor = Session.ResolveActor(Args.Num() >= 1 ? FCString::Atoi64(*Args[0]) : 0);
			if (!Actor)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.Anim.Dump: no such actor"));
				return;
			}
			UE_LOG(LogTemp, Warning, TEXT("swg.Anim.Dump: %s lookAt=%d aim=%d %s"), *Actor->GetName(),
				USWGCharacterAnimInstance::bLookAtEnabled, USWGCharacterAnimInstance::bAimEnabled, *Session.MeshGenerator->DescribeAnimPlayback(*Actor));
		}));

#endif
