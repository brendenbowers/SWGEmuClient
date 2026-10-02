#include "Subsystems/SWGCommandSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

// Dev: any command by name, e.g. "swg.Command teleport 1000 0 500" or
// "swg.Command burstrun". Everything after the name is the argument string.
static FAutoConsoleCommand SWGCommandCmd(
	TEXT("swg.Command"),
	TEXT("swg.Command <name> [arguments...] — queues a player command exactly as the toolbar would."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("Usage: swg.Command <name> [arguments...]"));
				return;
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

			const FString Arguments = FString::Join(TArrayView<const FString>(Args).RightChop(1), TEXT(" "));
			const int32 ActionCount = Commands->SendCommand(Args[0], 0, Arguments);
			UE_LOG(LogTemp, Warning, TEXT("swg.Command: sent '%s' '%s' as action %d"), *Args[0], *Arguments, ActionCount);
		}));
