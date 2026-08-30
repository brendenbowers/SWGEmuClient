#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGCommandSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGObjectGraphSubsystem;

/**
 * Sends player commands to the server — the path behind ability use, toolbar
 * presses and slash commands.
 *
 * Commands are identified on the wire by the hash of their lowercased name,
 * which is how Core3 registers them.
 */
UCLASS()
class SWGEMUCLIENT_API USWGCommandSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Queues a command by name ("burstrun", "attack"). TargetId 0 means no
	 * target. Returns the action count it was sent under — the server echoes
	 * that back on its reply — or 0 if there was nothing to send through.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Commands")
	int32 SendCommand(const FString& CommandName, int64 TargetId = 0, const FString& Arguments = FString());

	/** The CRC the server knows a command by. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Commands")
	static int64 HashCommandName(const FString& CommandName);

private:
	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	/** Per-command sequence echoed back on the server's reply. Starts at 1; 0 reads as "not sent". */
	uint32 NextActionCount = 1;
};
