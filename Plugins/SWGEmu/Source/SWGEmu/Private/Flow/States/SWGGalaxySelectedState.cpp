#include "Flow/States/SWGGalaxySelectedState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGMessageWaitSubsystem.h"
#include "Network/Messages/Zone/ClientIdMessage.h"
#include "Network/Messages/Zone/ClientPermissionsMessage.h"
#include "Network/Messages/Zone/ConnectPlayerMessage.h"
#include "Network/Messages/Zone/ConnectPlayerResponseMessage.h"
#include "Flow/SWGGalaxySelectedPayload.h"

void FSWGGalaxySelectedState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload)
{
	if (!UIStateMachine.Network || !UIStateMachine.WaitSubsystem)
	{
		return;
	}

	const TSharedPtr<FSWGGalaxySelectedPayload> GalaxySelectPayload = StaticCastSharedPtr<FSWGGalaxySelectedPayload>(Payload);
	if (!GalaxySelectPayload.IsValid())
	{
		return;
	}


	const FSWGGalaxyInfo* Galaxy = Ctx.Galaxies.FindByPredicate([GalaxyID = GalaxySelectPayload->GalaxyID](const FSWGGalaxyInfo& G)
		{
			return G.GalaxyID == GalaxyID;
		});

	if (!Galaxy)
	{
		UIStateMachine.Fail(TEXT("Selected galaxy not found"));
		return;
	}

	FString       GalaxyIP   = Galaxy->IP;
	int32         GalaxyPort = Galaxy->Port;
	TArray<uint8> SessionKey = Ctx.SessionToken;

	int32 Epoch = UIStateMachine.Epoch;
	TWeakObjectPtr<USWGClientFlowSubsystem> StateMchineWeakRef = &UIStateMachine;

	UIStateMachine.Network->Disconnect();

	UIStateMachine.Network->ConnectAsync(GalaxyIP, GalaxyPort)
		.Next([Epoch, StateMchineWeakRef, SessionKey, GalaxyID = GalaxySelectPayload->GalaxyID, &Ctx](TResult<void> Result)
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

				FClientIdMessage Msg;
				Msg.SessionKey = SessionKey;

				StateMachine->WaitSubsystem->SendAndWaitFor<FClientPermissionsMessage>(Msg.Serialize(), ESWGMessageOp::ClientPermissionsMessage)
					.Next([Epoch, StateMchineWeakRef, GalaxyID, &Ctx](TResult<TSharedPtr<const FClientPermissionsMessage>> Result)
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

							TSharedPtr<const FClientPermissionsMessage> Permissions = Result.GetValue();
							if (Permissions->GalaxyOpen == 0)
							{
								StateMachine->Fail(TEXT("Galaxy is closed"));
								return;
							}

							if (Permissions->CanCreateChar == 0 && Ctx.Characters.Num() == 0)
							{
								StateMachine->Fail(TEXT("Galaxy character creation is disabled"));
								return;
							}

							Ctx.SelectedGalaxyID = GalaxyID;

							// Never actually sent until now — this struct existed fully
							// implemented but unwired (same pattern as CmdSceneReady and
							// the client-initiated NetStatusRequest keepalive both turned
							// out to be). Per this message's own header comment, the
							// server's reply "triggers the SelectCharacter / CmdStartScene
							// sequence" — worth sending properly and waiting for the ack
							// rather than assuming the rest of the flow works fine without it.
							FConnectPlayerMessage ConnectMsg;
							StateMachine->WaitSubsystem->SendAndWaitFor<FConnectPlayerResponseMessage>(ConnectMsg.Serialize(), ESWGMessageOp::ConnectPlayerResponseMessage)
								.Next([Epoch, StateMchineWeakRef](TResult<TSharedPtr<const FConnectPlayerResponseMessage>> ConnectResult)
									{
										TStrongObjectPtr<USWGClientFlowSubsystem> InnerStateMachine = StateMchineWeakRef.Pin();
										if (!InnerStateMachine.IsValid() || InnerStateMachine->Epoch != Epoch)
										{
											return;
										}

										if (!ConnectResult.IsSuccess())
										{
											InnerStateMachine->Fail(ConnectResult.GetError());
											return;
										}

										InnerStateMachine->TransitionTo(ESWGClientState::CharacterSelect);
									});
						});
			});
}

void FSWGGalaxySelectedState::Exit(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}

REGISTER_FLOW_STATE(FSWGGalaxySelectedState, ESWGClientState::GalaxySelected)
