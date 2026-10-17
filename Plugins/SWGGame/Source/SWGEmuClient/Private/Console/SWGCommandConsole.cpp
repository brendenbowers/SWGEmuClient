#include "Subsystems/SWGCommandSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Objects/World/SWGShip.h"
#include "Common/SWGWorldScale.h"
#include "Misc/DefaultValueHelper.h"
#include "Objects/Player/SWGPlayer.h"

static FAutoConsoleCommand SWGShipViewCmd(
	TEXT("swg.ShipView"), TEXT("Toggle cockpit/exterior view for the ready local pilot (also bound to F)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		for (TObjectIterator<ASWGPlayer> It; It; ++It)
			if (IsValid(*It) && It->IsLocallyControlled()) { It->ToggleShipView(); return; }
	}));

static FAutoConsoleCommand SWGShipStatusCmd(
	TEXT("swg.ShipStatus"), TEXT("Reports local ship containment, readiness, pose and server flight limits."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		for (TObjectIterator<USWGObjectGraphSubsystem> It; It; ++It)
		{
			if (!IsValid(*It) || !It->GetGameInstance() || !It->GetLocalPlayerObjectId()) continue;
			ASWGShip* Ship = It->GetLocalPlayerShip();
			if (!Ship) continue;
			UE_LOG(LogTemp, Log, TEXT("ShipStatus player=%lld ship=%lld piloting=%d ready=%d predicting=%d speed=%.2f max=%.2f accel=%.2f decel=%.2f rates=%s position=%s"),
				It->GetLocalPlayerObjectId(), Ship->GetObjectId(), It->IsLocalPlayerPiloting(), It->IsLocalSpaceSceneReady(), Ship->bPredictingLocalFlight,
				Ship->SpaceVelocity.Size() / SWGWorldScale, Ship->ShipState.Flight.MaxSpeed, Ship->ShipState.Flight.Acceleration, Ship->ShipState.Flight.Deceleration,
				*Ship->ShipState.Flight.AngularRates.ToString(), *SWGToRawSpace(Ship->SpacePose.GetLocation()).ToString());
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("ShipStatus: no local ship"));
	}));

// Dev: any command by name, e.g. "swg.Command teleport 1000 0 500" or
// "swg.Command burstrun". --target selects the object used by terminal/seat commands.
static FAutoConsoleCommand SWGCommandCmd(
	TEXT("swg.Command"),
	TEXT("swg.Command <name> [--target objectId] [arguments...] — queues a player command exactly as the toolbar would."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("Usage: swg.Command <name> [--target objectId] [arguments...]"));
				return;
			}
			int64 TargetId = 0;
			int32 ArgumentStart = 1;
			if (Args.Num() > 1 && Args[1] == TEXT("--target"))
			{
				if (Args.Num() < 3 || !FDefaultValueHelper::ParseInt64(Args[2], TargetId) || TargetId <= 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.Command: --target requires a positive object ID"));
					return;
				}
				ArgumentStart = 3;
			}

			USWGCommandSubsystem* Commands = nullptr;
			for (TObjectIterator<USWGCommandSubsystem> It; It; ++It)
			{
				if (IsValid(*It) && It->GetGameInstance())
				{
					Commands = *It;
					break;
				}
			}
			if (!Commands)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.Command: no live command subsystem — not in a session yet"));
				return;
			}

			const FString Arguments = FString::Join(TArrayView<const FString>(Args).RightChop(ArgumentStart), TEXT(" "));
			const int32 ActionCount = Commands->SendCommand(Args[0], TargetId, Arguments);
			UE_LOG(LogTemp, Warning, TEXT("swg.Command: sent '%s' '%s' as action %d"), *Args[0], *Arguments, ActionCount);
		}));
