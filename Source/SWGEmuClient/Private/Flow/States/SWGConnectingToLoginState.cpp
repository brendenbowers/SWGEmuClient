#include "Flow/States/SWGConnectingToLoginState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGMessageWaitSubsystem.h"


void FSWGConnectingToLoginState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload)
{
	if (!UIStateMachine.Network)
	{
		return;
	}

	UIStateMachine.OnStatus.Broadcast(FText::FromString(TEXT("Connecting to login server...")));

	int32 Epoch = UIStateMachine.Epoch;
	TWeakObjectPtr<USWGClientFlowSubsystem> StateMchineWeakRef = &UIStateMachine;

	UIStateMachine.Network->ConnectAsync(Ctx.Host, 44453).Next([Epoch, StateMchineWeakRef, &Ctx](TResult<void> Result)
		{
			TStrongObjectPtr<USWGClientFlowSubsystem> StateMachine = StateMchineWeakRef.Pin();
			if (!StateMachine.IsValid() || StateMachine->Epoch != Epoch)
			{
				return;
			}

			if (Result.IsSuccess())
			{
				StateMachine->TransitionTo(ESWGClientState::Authenticating);
			}
			else
			{
				StateMachine->Fail(Result.GetError());
			}
		});
}
void FSWGConnectingToLoginState::Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx)
{

}

REGISTER_FLOW_STATE(FSWGConnectingToLoginState, ESWGClientState::ConnectingToLogin)
