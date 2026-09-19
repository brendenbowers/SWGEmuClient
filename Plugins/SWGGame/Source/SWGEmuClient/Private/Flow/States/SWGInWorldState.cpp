#include "Flow/States/SWGInWorldState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Flow/States/SWGZoneLoadingState.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/CmdStartSceneMessage.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Network/Messages/Zone/CmdSceneReadyMessage.h"
#include "Network/Messages/Zone/Object/DataTransform.h"
#include "Network/Messages/Zone/Object/DataTransformWithParent.h"
#include "Objects/World/SWGCell.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/Creature/SWGCreature.h"
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
	// Registered first so a scene change landing mid-Enter is still caught.
	if (UIStateMachine.Network)
	{
		TWeakObjectPtr<USWGClientFlowSubsystem> StateMachineWeak = &UIStateMachine;
		const int32 Epoch = UIStateMachine.Epoch;
		MessageHandle = UIStateMachine.Network->OnMessageReceived.AddLambda([StateMachineWeak, Epoch](TSharedPtr<FSWGNetMessage> Msg)
			{
				if (!Msg.IsValid() || Msg->Opcode != static_cast<uint32>(ESWGMessageOp::CmdStartScene))
				{
					return;
				}

				USWGClientFlowSubsystem* StateMachine = StateMachineWeak.Get();
				if (!StateMachine || StateMachine->Epoch != Epoch)
				{
					return;
				}

				UE_LOG(LogTemp, Log, TEXT("FSWGInWorldState: CmdStartScene while in world — reloading the scene"));
				TSharedPtr<FSWGTransitionPayload> ScenePayload = MakeShared<FSWGSceneStartPayload>(StaticCastSharedPtr<const FCmdStartSceneMessage>(Msg));
				StateMachine->TransitionTo(ESWGClientState::ZoneLoading, ScenePayload);
			});
	}

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
					const uint32 TimeStamp = (uint32)((uint64)(FPlatformTime::Seconds() * 1000.0) & 0xFFFFFFFFu);
					const FQuat Direction = SWGCharacterHeadingToNativeRotation(PlayerActor->GetActorRotation());

					// Zoned in inside a building: the server holds us in a cell, and a
					// world-space report would walk us out of it. Report relative to
					// the cell instead — cell-local is building-local. Until
					// ApplyContainment has composed the actor into the world, its
					// location still is the baseline's cell-relative one.
					const int64* ContainerId = ObjectGraph->FindContainerId(PlayerObjectId);
					const ASWGCell* Cell = ContainerId ? Cast<ASWGCell>(ObjectGraph->FindActor(*ContainerId)) : nullptr;
					const ASWGCreature* Creature = Cast<ASWGCreature>(PlayerActor);
					if (Cell)
					{
						FDataTransformWithParent Transform(PlayerObjectId);
						Transform.ParentId = (uint64)*ContainerId;
						const ASWGBuilding* Building = Cell->OwningBuilding.Get();
						const bool bPlaced = Creature && !Creature->bAwaitingCellPlacement && Building;
						// Feet, not capsule centre: Core3 bounces a cell report whose Z
						// is more than 25 cm off the floor. LastNetworkZ is the wire Z
						// (cell-relative until placement, world after).
						FVector Feet = PlayerActor->GetActorLocation();
						if (Creature)
						{
							Feet.Z = Creature->LastNetworkZ;
						}
						Transform.Position = SWGToRawSpace(bPlaced
							? Building->GetActorTransform().InverseTransformPosition(Feet)
							: Feet);
						Transform.Direction = Direction;
						Transform.TimeStamp = TimeStamp;
						Transform.MoveCount = 1;
						Transform.Speed = 0.0f;

						UIStateMachine.Network->SendMessage(Transform.Serialize());
						UE_LOG(LogTemp, Log, TEXT("FSWGInWorldState::Enter: sent DataTransformWithParent for object %lld in cell %lld at %s"),
							PlayerObjectId, *ContainerId, *Transform.Position.ToString());
					}
					else
					{
						FDataTransform Transform(PlayerObjectId);
						// Server expects raw (pre-scale) wire-space coordinates, same as
						// every position it sends us — convert before sending, same as
						// ASWGPlayer::SendDataTransformUpdate.
						Transform.Position = SWGToRawSpace(PlayerActor->GetActorLocation());
						Transform.Direction = Direction;
						Transform.TimeStamp = TimeStamp;
						Transform.MoveCount = 1;
						Transform.Speed = 0.0f;

						UIStateMachine.Network->SendMessage(Transform.Serialize());
						UE_LOG(LogTemp, Log, TEXT("FSWGInWorldState::Enter: sent DataTransform for object %lld at %s"),
							PlayerObjectId, *Transform.Position.ToString());
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("FSWGInWorldState::Enter: could not find player actor %lld — DataTransform NOT sent"), PlayerObjectId);
				}
			}
		}
	}
}
void FSWGInWorldState::Exit(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx)
{
	if (UIStateMachine.Network && MessageHandle.IsValid())
	{
		UIStateMachine.Network->OnMessageReceived.Remove(MessageHandle);
	}
	MessageHandle.Reset();
}

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
