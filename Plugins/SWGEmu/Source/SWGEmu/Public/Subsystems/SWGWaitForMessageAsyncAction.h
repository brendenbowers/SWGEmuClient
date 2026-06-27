#pragma once

#include "CoreMinimal.h"
#include "Engine/CancellableAsyncAction.h"
#include "Common/ResultTypes.h"
#include "Network/Messages/SWGNetMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "SWGWaitForMessageAsyncAction.generated.h"

class USWGMessageWaitSubsystem;

/**
 * USWGWaitForMessageAsyncAction — Blueprint node for awaiting network messages.
 *
 * Wraps USWGMessageWaitSubsystem::WaitForMessage<T>. Derives from
 * UCancellableAsyncAction so Blueprint panels can cancel a pending wait when
 * they deactivate (e.g. CommonUI stack pop).
 */
UCLASS()
class SWGEMU_API USWGWaitForMessageAsyncAction : public UCancellableAsyncAction
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
	virtual void Cancel() override;

	// ── Internals ─────────────────────────────────────────────────

private:
	uint32 Opcode = 0;
	TWeakObjectPtr<USWGMessageWaitSubsystem> WeakWaitSubsystem;

	void OnMessageReceived(TSharedPtr<const FSWGNetMessage> Msg);
	void OnWaitFailure(const FString& Reason);

	friend class USWGNetworkBlueprintLibrary;
};
