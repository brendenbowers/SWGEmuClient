#include "Flow/States/SWGDisconnectedState.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGMessageWaitSubsystem.h"

void FSWGDisconnectedState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) 
{
	if (UIStateMachine.Network)
	{
		UIStateMachine.Network->Disconnect();
	}

	if (UIStateMachine.WaitSubsystem)
	{
		UIStateMachine.WaitSubsystem->CancelAll(TEXT("Disconnected"));
	}

	UIStateMachine.OnStatus.Broadcast(LexToText(ESWGClientState::Disconnected));
}
void FSWGDisconnectedState::Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}
