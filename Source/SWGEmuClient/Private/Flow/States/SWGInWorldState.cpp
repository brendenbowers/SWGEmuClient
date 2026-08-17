#include "Flow/States/SWGInWorldState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Network/Messages/Zone/CmdSceneReadyMessage.h"
#include "Network/Messages/Zone/Object/DataTransform.h"
#include "Network/Messages/Zone/Object/TeleportAck.h"
#include "Common/SWGWorldScale.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SWGMessageWaitSubsystem.h"
#include "Network/Messages/Zone/SceneEndBaselinesMessage.h"
#include "Objects/Player/SWGPlayer.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGEquipmentComponent.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"
#include "SaveData/SWGCharacterPreviewSaveGame.h"
#include "Kismet/GameplayStatics.h"

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

	// Core3's Zone::inRange awareness recompute is only triggered by
	// CreatureObject::updateZone(), which the server only calls in reaction to
	// the client's own DataTransform report. The initial zone-in teleport() does
	// one scan automatically but nothing else prompts a second one for a
	// stationary client, so send one "stationary" report right after zone-in.
	if (UIStateMachine.Network)
	{
		if (UGameInstance* GameInstance = UIStateMachine.GetGameInstance())
		{

			if (USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>())
			{
				const int64 PlayerObjectId = ObjectGraph->GetLocalPlayerObjectId();
				if (USWGMessageWaitSubsystem* WaitSubsystem = GameInstance->GetSubsystem<USWGMessageWaitSubsystem>())
				{
					WaitSubsystem->WaitForMessage<FSceneEndBaselinesMessage>(ESWGMessageOp::SceneEndBaselines, 10.0f, [PlayerObjectId](const FSWGNetMessage& Msg) 
						{
							return static_cast<const FSceneEndBaselinesMessage*>(&Msg)->ObjectId == PlayerObjectId;
						}).Next([&UIStateMachine, Ctx](TResult<TSharedPtr<const FSceneEndBaselinesMessage>> EndMsg) mutable
							{
								HandleSaveCharacterCache(UIStateMachine, Ctx, EndMsg);
							});
				}


				// Core3 sets PlayerObject::isTeleporting on every zone-in
				// (PlayerZoneComponent::switchZone) and DataTransformCallback::run
				// rejects every movement update afterward with "!teleporting" until it
				// sees this ack (TeleportAckCallback::run -> setTeleporting(false)).
				// Without it every DataTransform we ever send post-login is silently
				// dropped, forever — not an actual teleport, just a stuck flag.
				FTeleportAck Ack(PlayerObjectId);
				Ack.MoveCount = 1;
				UIStateMachine.Network->SendMessage(Ack.Serialize());
				UE_LOG(LogTemp, Log, TEXT("FSWGInWorldState::Enter: sent TeleportAck for object %lld"), PlayerObjectId);

				if (const AActor* PlayerActor = ObjectGraph->FindActor(PlayerObjectId))
				{
					FDataTransform Transform(PlayerObjectId);
					// Server expects raw (pre-scale) wire-space coordinates, same as
					// every position it sends us — convert before sending, same as
					// ASWGPlayer::SendDataTransformUpdate.
					Transform.Position = SWGToRawSpace(PlayerActor->GetActorLocation());
					Transform.Direction = PlayerActor->GetActorQuat();
					Transform.TimeStamp = (uint32)((uint64)(FPlatformTime::Seconds() * 1000.0) & 0xFFFFFFFFu);
					Transform.MoveCount = 1;
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

void FSWGInWorldState::HandleSaveCharacterCache(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, TResult<TSharedPtr<const FSceneEndBaselinesMessage>> Msg)
{

	if (Msg.IsFailure())
	{
		const FString& Error = Msg.GetError();
		UE_LOG(LogTemp, Verbose, TEXT("FSWGInWorldState: %s"), *Error);
		return;
	}

	UGameInstance* GameInstance = UIStateMachine.GetGameInstance();
	if(!GameInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGInWorldState: Failed to get Game instance when trying to save character cacahe"));
		return;
	}

	USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>();
	if (!ObjectGraph)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGInWorldState: Failed to get Object Graph when trying to save character cacahe"));
		return;
	}

	int64 ObjectID = Msg.GetValue()->ObjectId;

	ASWGPlayer* Player = Cast<ASWGPlayer>(ObjectGraph->FindActor(ObjectID));
	if (!Player)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGInWorldState: Failed to get Player for object id %d"), ObjectID);
		return;
	}

	FSWGCachedCharacterPreview* CharacterCache = Ctx.CharacterCache->Characters.FindByPredicate([ObjectID](const FSWGCachedCharacterPreview& CachedCharacter) { return CachedCharacter.CharacterID == ObjectID; });
	if (!CharacterCache)
	{
		CharacterCache = &Ctx.CharacterCache->Characters.AddDefaulted_GetRef();
	}

	CharacterCache->CharacterID = ObjectID;
	CharacterCache->BodyTemplateCRC = Player->GetObjectCrc();
	CharacterCache->CharacterName = Player->TangibleComponent->CustomName;
	CharacterCache->CustomizationBytes = Player->TangibleComponent->CustomizationBytes;
	CharacterCache->AlternateAppearance = Player->EquipmentComponent->AlternateAppearance;
	CharacterCache->Equipment.Reset();

	for (const FEquiptmentItem& Item : Player->EquipmentComponent->EquipmentList.Items)
	{
		if (!SWGIsSlottedArrangement(Item.ContainmentType))
		{
			continue;
		}

		FSWGCachedEquipment& CachedEquipment = CharacterCache->Equipment.AddDefaulted_GetRef();
		CachedEquipment.TemplateCRC = Item.TemplateCRC;
		CachedEquipment.ContainmentType = Item.ContainmentType;
		CachedEquipment.CustomizationBytes = Item.CustomizationBytes;
	}

	UGameplayStatics::AsyncSaveGameToSlot(Ctx.CharacterCache.Get(), TEXT("CharacterPreviews"), 0);
}

REGISTER_FLOW_STATE(FSWGInWorldState, ESWGClientState::InWorld)
