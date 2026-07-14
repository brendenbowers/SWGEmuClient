#include "Flow/States/SWGCharacterSelectState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

void FSWGCharacterSelectState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload) {}
void FSWGCharacterSelectState::Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}

REGISTER_FLOW_STATE(FSWGCharacterSelectState, ESWGClientState::CharacterSelect)
