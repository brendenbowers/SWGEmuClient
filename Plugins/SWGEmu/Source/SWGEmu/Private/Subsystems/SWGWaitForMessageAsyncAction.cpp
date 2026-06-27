#include "Subsystems/SWGWaitForMessageAsyncAction.h"
#include "Subsystems/SWGMessageWaitSubsystem.h"
#include "Kismet/GameplayStatics.h"

void USWGWaitForMessageAsyncAction::Cancel()
{
	Super::Cancel();
	SetReadyToDestroy();
}

void USWGWaitForMessageAsyncAction::Activate()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		OnWaitFailure(TEXT("No world context"));
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		OnWaitFailure(TEXT("No game instance"));
		return;
	}

	USWGMessageWaitSubsystem* WaitSubsystem = GameInstance->GetSubsystem<USWGMessageWaitSubsystem>();
	if (!WaitSubsystem)
	{
		OnWaitFailure(TEXT("SWGMessageWaitSubsystem not available"));
		return;
	}

	WeakWaitSubsystem = WaitSubsystem;

	ESWGMessageOp OpEnum = static_cast<ESWGMessageOp>(Opcode);

	// Clamp TimeoutSeconds to be at least 1 second
	float Timeout = FMath::Max(TimeoutSeconds, 1.f);

	// Register the wait; use Next() to fire our completion delegate on the game thread
	WaitSubsystem->WaitForMessage<FSWGNetMessage>(OpEnum, Timeout)
		.Next([this](TResult<TSharedPtr<const FSWGNetMessage>> Result)
		{
			if (Result.IsFailure())
			{
				OnWaitFailure(Result.GetError());
			}
			else
			{
				OnMessageReceived(Result.GetValue());
			}
		});
}

void USWGWaitForMessageAsyncAction::OnMessageReceived(TSharedPtr<const FSWGNetMessage> Msg)
{
	if (!Msg.IsValid())
	{
		OnWaitFailure(TEXT("Received null message"));
		return;
	}

	// Serialize the raw message bytes into a TArray<uint8> for Blueprint consumption
	TArray<uint8> MessageBytes;
	if (Msg->Opcode > 0)
	{
		// Write opcode (4 bytes, little-endian)
		MessageBytes.Add((uint8)((Msg->Opcode >> 0) & 0xFF));
		MessageBytes.Add((uint8)((Msg->Opcode >> 8) & 0xFF));
		MessageBytes.Add((uint8)((Msg->Opcode >> 16) & 0xFF));
		MessageBytes.Add((uint8)((Msg->Opcode >> 24) & 0xFF));

		// Write payload if available (this is a simplified version;
		// real implementation would depend on FSWGNetMessage structure)
	}

	OnSuccess.Broadcast(MessageBytes);
	SetReadyToDestroy();
}

void USWGWaitForMessageAsyncAction::OnWaitFailure(const FString& Reason)
{
	bool bTimedOut = Reason.Contains(TEXT("Timed out"));
	OnFailure.Broadcast(bTimedOut, Reason);
	SetReadyToDestroy();
}
