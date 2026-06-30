#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

FSWGFlowStateRegistry& FSWGFlowStateRegistry::Get()
{
	static FSWGFlowStateRegistry Registry;
	return Registry;
}

void FSWGFlowStateRegistry::RegisterStateDefinition(ESWGClientState StateType, TFunction<TSharedPtr<ISWGFlowState>()> Factory, ESWGClientState PreviousState)
{
	StateDefinitions.Add({StateType, PreviousState, Factory});
}

void FSWGFlowStateRegistry::RegisterAllStates(USWGClientFlowSubsystem& Subsystem)
{
	for (const FStateDefinition& Def : StateDefinitions)
	{
		Subsystem.RegisterState(Def.StateType, Def.Factory(), Def.PreviousState);
	}
}
