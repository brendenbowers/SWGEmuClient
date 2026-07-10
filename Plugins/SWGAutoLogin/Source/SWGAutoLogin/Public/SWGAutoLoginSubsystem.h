#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Flow/SWGClientState.h"
#include "SWGAutoLoginSubsystem.generated.h"

class USWGClientFlowSubsystem;

/**
 * Temporary dev-convenience subsystem — drives USWGClientFlowSubsystem
 * through login automatically (localhost/test/test, first galaxy, first
 * character) instead of requiring manual UI interaction on every PIE run.
 * Lives in its own plugin so it can be disabled/removed without touching
 * SWGEmu itself once real login UI testing is needed.
 */
UCLASS()
class SWGAUTOLOGIN_API USWGAutoLoginSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	UFUNCTION()
	void HandleStateChanged(ESWGClientState OldState, ESWGClientState NewState);

	UPROPERTY()
	TObjectPtr<USWGClientFlowSubsystem> FlowSubsystem;
};
