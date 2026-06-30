#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGFlowStateRegistrationSubsystem.generated.h"

class USWGClientFlowSubsystem;

/**
 * Registers all flow state definitions with the client flow subsystem.
 * This subsystem depends on USWGClientFlowSubsystem and is responsible for
 * populating it with all registered states at initialization time.
 * This keeps the flow subsystem decoupled from the state registry.
 */
UCLASS()
class SWGEMU_API USWGFlowStateRegistrationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
};
