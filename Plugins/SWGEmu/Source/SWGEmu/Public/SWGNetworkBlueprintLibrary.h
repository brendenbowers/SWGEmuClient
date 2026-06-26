#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWGNetworkBlueprintLibrary.generated.h"

class USWGWaitForMessageAsyncAction;

/**
 * USWGNetworkBlueprintLibrary — Blueprint utilities for SWG networking.
 *
 * Provides Blueprint-callable functions for common network operations.
 */
UCLASS()
class SWGEMU_API USWGNetworkBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Wait for a network message asynchronously (Blueprint-callable).
	 *
	 * @param WorldContext      World context object.
	 * @param MessageOpcode     The opcode to wait for (32-bit CRC value as int32).
	 * @param TimeoutSeconds    Timeout duration in seconds.
	 * @return                  Async action node that fires OnSuccess or OnFailure.
	 *
	 * Usage (Blueprint):
	 *   - Call this function to create an async action
	 *   - Connect the OnSuccess and OnFailure event pins to your logic
	 *
	 * Example opcodes (from ESWGMessageOp):
	 *   - 0xC11C63B9 = LoginEnumCluster
	 *   - 0x3436AEB6 = LoginClusterStatus
	 *   - 0x65EA4574 = EnumerateCharacterId
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Network", meta = (WorldContext = "WorldContext"))
	static USWGWaitForMessageAsyncAction* BP_WaitForMessage(
		const UObject* WorldContext,
		int32 MessageOpcode,
		float TimeoutSeconds = 10.f);
};
