#include "Subsystems/SWGFlowStateRegistrationSubsystem.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Flow/SWGFlowStateRegistry.h"

void USWGFlowStateRegistrationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Depend on the flow subsystem so it initializes first.
	USWGClientFlowSubsystem* FlowSubsystem = Cast<USWGClientFlowSubsystem>(
		Collection.InitializeDependency(USWGClientFlowSubsystem::StaticClass()));

	if (FlowSubsystem)
	{
		// Register all state definitions with the flow subsystem.
		FSWGFlowStateRegistry::Get().RegisterAllStates(*FlowSubsystem);
	}
}
