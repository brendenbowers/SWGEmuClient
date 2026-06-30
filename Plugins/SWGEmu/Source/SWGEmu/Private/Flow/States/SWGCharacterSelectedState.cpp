#include "Flow/States/SWGCharacterSelectedState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Flow/States/SWGZoneLoadingState.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGMessageWaitSubsystem.h"
#include "Network/Messages/Zone/SelectCharacterMessage.h"
#include "Network/Messages/Zone/CmdStartSceneMessage.h"
#include "Flow/SWGCharacterSelectedPayload.h"

void FSWGCharacterSelectedState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload)
{
	if (!UIStateMachine.Network || !UIStateMachine.WaitSubsystem)
	{
		return;
	}

	const TSharedPtr<FSWGCharacterSelectedPayload> CharacterSelectPayload = StaticCastSharedPtr<FSWGCharacterSelectedPayload>(Payload);

	int32 Epoch = UIStateMachine.Epoch;
	TWeakObjectPtr<USWGClientFlowSubsystem> StateMchineWeakRef = &UIStateMachine;

	FSelectCharacterMessage Msg;
	Msg.CharacterID = CharacterSelectPayload->CharacterID;

	UIStateMachine.WaitSubsystem->SendAndWaitFor<FCmdStartSceneMessage>(Msg.Serialize(), ESWGMessageOp::CmdStartScene)
		.Next([Epoch, StateMchineWeakRef, CharacterID = CharacterSelectPayload->CharacterID, &Ctx](TResult<TSharedPtr<const FCmdStartSceneMessage>> Result)
			{
				TStrongObjectPtr<USWGClientFlowSubsystem> StateMachine = StateMchineWeakRef.Pin();
				if (!StateMachine.IsValid() || StateMachine->Epoch != Epoch)
				{
					return;
				}

				if (!Result.IsSuccess())
				{
					StateMachine->Fail(Result.GetError());
					return;
				}

				Ctx.SelectedCharacterID = CharacterID;

				TSharedPtr<FSWGTransitionPayload> ScenePayload = MakeShared<FSWGSceneStartPayload>(Result.GetValue());
				StateMachine->TransitionTo(ESWGClientState::ZoneLoading, ScenePayload);
			});
}

void FSWGCharacterSelectedState::Exit(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}

REGISTER_FLOW_STATE(FSWGCharacterSelectedState, ESWGClientState::CharacterSelected)
