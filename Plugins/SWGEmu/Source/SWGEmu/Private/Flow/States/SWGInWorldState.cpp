#include "Flow/States/SWGInWorldState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Network/Messages/Zone/CmdSceneReadyMessage.h"

void FSWGInWorldState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload)
{
	// Tells the zone server the client finished loading the scene (CmdStartScene ->
	// Create/Baselines/EndBaselines for own CREO+PLAY and nearby objects -> here).
	// Server gates gameplay (combat/chat/trade) on receiving this — see
	// world-object-plan.html "Minimal zone-in sequence".
	const FCmdSceneReadyMessage Msg;
	UIStateMachine.Network->SendMessage(Msg.Serialize());
}
void FSWGInWorldState::Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}

REGISTER_FLOW_STATE(FSWGInWorldState, ESWGClientState::InWorld)
