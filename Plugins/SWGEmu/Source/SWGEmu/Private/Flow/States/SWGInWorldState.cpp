#include "Flow/States/SWGInWorldState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

void FSWGInWorldState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload) {}
void FSWGInWorldState::Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}

REGISTER_FLOW_STATE(FSWGInWorldState, ESWGClientState::InWorld)
