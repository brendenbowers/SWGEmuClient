#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Common/ResultTypes.h"
#include "Network/Messages/SWGNetMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "SWGWaitForMessageAsyncAction.generated.h"

class USWGMessageWaitSubsystem;

/**
 * USWGWaitForMessageAsyncAction — Blueprint node for awaiting network messages.
 *
 * Wraps USWGMessageWaitSubsystem::WaitForMessage<T> with weak-pointer guards
 * to ensure the action doesn't keep the subsystem alive if the caller goes away.
 *
 * Usage (Blueprint):
 *   - "Wait For Message" node
 *   - Set Opcode to the message you're waiting for
 *   - Set TimeoutSeconds
 *   - Pin: OnSuccess fires with the message; OnFailure fires with error string
 *
 * The action self-destructs when the message arrives or timeout expires.
 */
UCLASS()
class SWGEMU_API USWGWaitForMessageAsyncAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	/** Timeout in seconds. */
	UPROPERTY(BlueprintReadWrite, Category = "SWGEmu|Network")
	float TimeoutSeconds = 10.f;

	// ── Output Delegates ──────────────────────────────────────────

	/** Fired on the game thread when a matching message arrives. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMessageReceived, TArray<uint8>, MessageData);
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Network")
	FOnMessageReceived OnSuccess;

	/** Fired if the wait times out or the network disconnects. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFailure, bool, bTimedOut, FString, ErrorMessage);
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Network")
	FOnFailure OnFailure;

	// ── Activation ────────────────────────────────────────────────

	virtual void Activate() override;

	// ── Internals ─────────────────────────────────────────────────

private:
	uint32 Opcode = 0;
	TWeakObjectPtr<USWGMessageWaitSubsystem> WeakWaitSubsystem;

	void OnMessageReceived(TSharedPtr<const FSWGNetMessage> Msg);
	void OnWaitFailure(const FString& Reason);

	friend class USWGNetworkBlueprintLibrary;
};
