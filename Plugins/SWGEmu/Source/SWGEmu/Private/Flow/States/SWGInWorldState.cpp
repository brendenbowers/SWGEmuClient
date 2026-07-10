#include "Flow/States/SWGInWorldState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Network/Messages/Zone/CmdSceneReadyMessage.h"
#include "Network/Messages/Zone/DataTransformMessage.h"
#include "Engine/GameInstance.h"

void FSWGInWorldState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload)
{
	// Tells the zone server the client finished loading the scene (CmdStartScene ->
	// Create/Baselines/EndBaselines for own CREO+PLAY and nearby objects -> here).
	// Server gates gameplay (combat/chat/trade) on receiving this — see
	// world-object-plan.html "Minimal zone-in sequence".
	const FCmdSceneReadyMessage Msg;
	if (UIStateMachine.Network)
	{
		UIStateMachine.Network->SendMessage(Msg.Serialize());
		UE_LOG(LogTemp, Log, TEXT("FSWGInWorldState::Enter: sent CmdSceneReadyMessage"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGInWorldState::Enter: UIStateMachine.Network was null — CmdSceneReadyMessage NOT sent"));
	}

	// Core3's Zone::inRange awareness recompute (what actually decides which
	// nearby objects — including buildings — get sent to us) is only ever
	// triggered by CreatureObject::updateZone(), which the server ONLY calls
	// from DataTransformCallback::run() — i.e. only in reaction to the client
	// sending its own position/movement report. The initial zone-in teleport()
	// does one scan automatically, but nothing ever prompts a second one for a
	// stationary client — confirmed this client has never sent this message at
	// all. Send one "stationary" report right after zone-in to trigger that
	// recompute and see if anything (buildings included) that the first scan
	// missed shows up afterward.
	if (UIStateMachine.Network)
	{
		if (UGameInstance* GameInstance = UIStateMachine.GetGameInstance())
		{
			if (USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>())
			{
				const int64 PlayerObjectId = ObjectGraph->GetLocalPlayerObjectId();
				if (const AActor* PlayerActor = ObjectGraph->FindActor(PlayerObjectId))
				{
					FDataTransformMessage Transform;
					Transform.ObjectId = PlayerObjectId;
					Transform.Position = PlayerActor->GetActorLocation();
					Transform.Direction = PlayerActor->GetActorQuat();
					Transform.TimeStamp = (uint32)((uint64)(FPlatformTime::Seconds() * 1000.0) & 0xFFFFFFFFu);
					Transform.MovementCounter = 1;
					Transform.Speed = 0.0f;

					UIStateMachine.Network->SendMessage(Transform.Serialize());
					UE_LOG(LogTemp, Log, TEXT("FSWGInWorldState::Enter: sent DataTransform for object %lld at %s"),
						PlayerObjectId, *Transform.Position.ToString());
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("FSWGInWorldState::Enter: could not find player actor %lld — DataTransform NOT sent"), PlayerObjectId);
				}
			}
		}
	}
}
void FSWGInWorldState::Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}

REGISTER_FLOW_STATE(FSWGInWorldState, ESWGClientState::InWorld)
