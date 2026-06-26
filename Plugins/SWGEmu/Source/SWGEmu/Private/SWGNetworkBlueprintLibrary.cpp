#include "SWGNetworkBlueprintLibrary.h"
#include "Subsystems/SWGWaitForMessageAsyncAction.h"

USWGWaitForMessageAsyncAction* USWGNetworkBlueprintLibrary::BP_WaitForMessage(
	const UObject* WorldContext,
	int32 MessageOpcode,
	float TimeoutSeconds)
{
	if (!WorldContext)
	{
		UE_LOG(LogTemp, Warning, TEXT("BP_WaitForMessage: Invalid world context"));
		return nullptr;
	}

	UWorld* World = WorldContext->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("BP_WaitForMessage: No world context"));
		return nullptr;
	}

	USWGWaitForMessageAsyncAction* AsyncAction = NewObject<USWGWaitForMessageAsyncAction>(GetTransientPackageAsObject());
	if (!AsyncAction)
	{
		UE_LOG(LogTemp, Warning, TEXT("BP_WaitForMessage: Failed to create async action"));
		return nullptr;
	}

	AsyncAction->RegisterWithGameInstance(World->GetGameInstance());
	AsyncAction->Opcode = MessageOpcode;
	AsyncAction->TimeoutSeconds = TimeoutSeconds;
	AsyncAction->Activate();

	return AsyncAction;
}
