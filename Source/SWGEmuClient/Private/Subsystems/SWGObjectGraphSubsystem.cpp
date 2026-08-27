

#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGFormTagMapping.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/SceneCreateObjectMessage.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/Zone/SceneEndBaselinesMessage.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/Zone/CmdStartSceneMessage.h"
#include "Network/Messages/Zone/UpdateContainmentMessage.h"
#include "Network/Messages/Zone/UpdateTransformMessage.h"
#include "Network/Messages/Zone/ObjControllerMessageIn.h"
#include "Network/Messages/Zone/Object/TeleportAck.h"

#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Objects/Creature/SWGCreature.h"

#include "Objects/SWGNetworkObjectInterface.h"
#include "Objects/Tangible/SWGItem.h"
#include "Objects/Creature/SWGCreature.h"
#include "Objects/Player/SWGPlayer.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "Objects/World/SWGInstallation.h"
#include "Objects/World/SWGStaticProp.h"
#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "Subsystems/SWGBaselineHandlerRegistry.h"
#include "Network/Messages/SWGFourCC.h"

#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"
#include "Components/SWGHealthComponent.h"
#include "Components/SWGSkillComponent.h"
#include "Components/SWGEncumbranceComponent.h"
#include "Components/SWGSpaceMissionComponent.h"
#include "Components/SWGEquipmentComponent.h"
#include "Components/SWGCombatStateComponent.h"
#include "Components/SWGGroupComponent.h"
#include "Components/SWGPerformanceComponent.h"
#include "Components/SWGMovementComponent.h"

#include "Engine/LevelStreaming.h"
#include <Subsystems/SWGActorSpawnHandlerRegistry.h>

namespace
{
	void ApplyTangibleDeltas(ASWGItem& Item, uint8 Slot, FSWGPacket& Packet)
	{
		switch (Slot)
		{
		case 3:
			if (Item.TangibleComponent)
			{
				//Item.TangibleComponent->ApplyBase3Part1(Packet);
			}
			if (Item.ConditionComponent)
			{
				//Item.ConditionComponent->ApplyBase3(Packet);
			}
			if (Item.TangibleComponent)
			{
				//Item.TangibleComponent->ApplyBase3Part2(Packet);
			}
			break;
		case 6:
			if (Item.DefenderComponent)
			{
				//Item.DefenderComponent->ApplyBase6(Packet);
			}
			break;
		default:
			UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: no TANO delta dispatch for slot %d"), Slot);
			break;
		}
	}
}

void USWGObjectGraphSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Network = Cast<USWGNetworkSubsystem>(Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));
	MeshGenerator = Cast<USWGMeshGeneratorSubsystem>(Collection.InitializeDependency(USWGMeshGeneratorSubsystem::StaticClass()));
	TerrainSubsystem = Cast<USWGTerrainSubsystem>(Collection.InitializeDependency(USWGTerrainSubsystem::StaticClass()));
	TreSubsystem = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGObjectGraphSubsystem::HandleMessageReceived);
	}
}

void USWGObjectGraphSubsystem::Deinitialize()
{
	if (Network && MessageHandle.IsValid())
	{
		Network->OnMessageReceived.Remove(MessageHandle);
		MessageHandle.Reset();
	}

	ActorRegistry.Reset();
	CrcToActorClass.Reset();
	bCrcMapBuilt = false;
}

void USWGObjectGraphSubsystem::Tick(float DeltaTime)
{}

TStatId USWGObjectGraphSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGObjectGraphSubsystem, STATGROUP_Tickables);
}

bool USWGObjectGraphSubsystem::IsTickable() const
{
	return true;
}

void USWGObjectGraphSubsystem::SetCrcToActorClassMap(TMap<uint32, TSubclassOf<AActor>> InMap)
{
	CrcToActorClass = MoveTemp(InMap);
	bCrcMapBuilt = true;

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: adopted CRC->actor-class map with %d entries"), CrcToActorClass.Num());
}

TSubclassOf<AActor> USWGObjectGraphSubsystem::ResolveActorClassForCrc(uint32 Crc) const
{
	if (const TSubclassOf<AActor>* Found = CrcToActorClass.Find(Crc))
		return *Found;
	return nullptr;
}

void USWGObjectGraphSubsystem::SetCurrentZoneLevel(ULevelStreaming* Streaming)
{
	CurrentZoneStreamingLevel = Streaming;
}

ULevel* USWGObjectGraphSubsystem::GetSpawnLevel() const
{
	if (ULevelStreaming* Streaming = CurrentZoneStreamingLevel.Get())
	{
		return Streaming->GetLoadedLevel();
	}
	else
	{
		GetWorld()->GetLevel(0); // PersistentLevel
	}
	return nullptr;
}

void USWGObjectGraphSubsystem::RevealCurrentZoneLevel()
{
	//ULevelStreaming* Streaming = CurrentZoneStreamingLevel.Get();
	//if (!Streaming)
	//{
	//	UE_LOG(LogTemp, Warning, TEXT("USWGObjectGraphSubsystem: RevealCurrentZoneLevel called with no zone level set"));
	//	return;
	//}

	//Streaming->SetShouldBeVisible(true);
	OnZoneLevelRevealed.Broadcast();

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: zone level revealed"));
}

FVector USWGObjectGraphSubsystem::GroundedLocationFor(const AActor* Actor, const FVector& NetworkPos)
{
	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		// Stash the server's real feet-level Z regardless of capsule state —
		// USWGMeshGeneratorSubsystem's capsule-resize step reads this back
		// directly instead of reverse-engineering it from the actor's
		// current location (which can drift due to an unconstrained
		// freefall before real terrain collision exists, physics, etc. — see
		// ASWGCreature::LastNetworkZ's own comment for why that mattered).
		if (ASWGCreature* Creature = const_cast<ASWGCreature*>(Cast<ASWGCreature>(Actor)))
		{
			Creature->LastNetworkZ = NetworkPos.Z;
		}

		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			return NetworkPos + FVector(0.0f, 0.0f, HalfHeight);
		}
		UE_LOG(LogTemp, Warning, TEXT("GroundedLocationFor: actor=%s is ACharacter but GetCapsuleComponent() is null"), *Actor->GetName());
	}
	return NetworkPos;
}

AActor* USWGObjectGraphSubsystem::FindActor(int64 ObjectId) const
{
	if (const TWeakObjectPtr<AActor>* Found = ActorRegistry.Find(ObjectId))
		return Found->Get();
	return nullptr;
}

void USWGObjectGraphSubsystem::OnZoneLevelLoaded()
{
	bLevelReadyForObjects = true;

	TArray<TSharedPtr<FSWGNetMessage>> Replay = MoveTemp(PendingMessages);
	PendingMessages.Reset();

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: zone level loaded, replaying %d buffered message(s)"), Replay.Num());

	for (const TSharedPtr<FSWGNetMessage>& Msg : Replay)
	{
		HandleMessageReceived(Msg);
	}
}

void USWGObjectGraphSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg)
{
	if (!Msg.IsValid())
		return;

	const uint32 Opcode = Msg->Opcode;

	if (Opcode == static_cast<uint32>(ESWGMessageOp::CmdStartScene))
	{
		// A new zone load is starting — UGameplayStatics::OpenLevel (triggered
		// by this same message, in FSWGZoneLoadingState::Enter) won't actually
		// swap levels until the next world-travel tick, so buffer everything
		// else until OnZoneLevelLoaded() confirms the new level is live. See
		// that function's header comment for the full story.
		bLevelReadyForObjects = false;
		HandleCmdStartScene(*static_cast<const FCmdStartSceneMessage*>(Msg.Get()));
		return;
	}

	if (!bLevelReadyForObjects)
	{
		PendingMessages.Add(Msg);
		return;
	}

	if (Opcode == static_cast<uint32>(ESWGMessageOp::SceneCreateObjectByCrc))
	{
		HandleSceneCreateObject(*static_cast<const FSceneCreateObjectMessage*>(Msg.Get()));
	}
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::BaselinesMessage))
	{
		HandleBaselines(*static_cast<const FBaselinesMessage*>(Msg.Get()));
	}
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::SceneEndBaselines))
	{
		HandleSceneEndBaselines(*static_cast<const FSceneEndBaselinesMessage*>(Msg.Get()));
	}
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::DeltasMessage))
	{
		HandleDeltas(*static_cast<const FDeltasMessage*>(Msg.Get()));
	}
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::UpdateContainmentMessage))
	{
		HandleUpdateContainment(*static_cast<const FUpdateContainmentMessage*>(Msg.Get()));
	}
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::UpdateTransformMessage))
	{
		HandleUpdateTransform(*static_cast<const FUpdateTransformMessage*>(Msg.Get()));
	}
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::ObjControllerMessage))
	{
		HandleObjControllerMessage(*static_cast<const FObjControllerMessageIn*>(Msg.Get()));
	}
}

void USWGObjectGraphSubsystem::HandleCmdStartScene(const FCmdStartSceneMessage& Msg)
{
	// CharacterID doubles as the ObjectId of the local player's own CREO in the
	// SceneCreateObjectByCrc/BaselinesMessage stream that follows — this is how
	// we know which spawned ASWGCreature should actually be an ASWGPlayer.
	LocalPlayerObjectId = Msg.CharacterID;

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: local player ObjectId set to %lld from CmdStartScene"), LocalPlayerObjectId);
}

void USWGObjectGraphSubsystem::HandleSceneCreateObject(const FSceneCreateObjectMessage& Msg)
{
	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: SceneCreateObjectByCrc object=%lld crc=%08X pos=(%.1f,%.1f,%.1f)"),
		Msg.ObjectId, Msg.ObjectCrc, Msg.PosX, Msg.PosY, Msg.PosZ);

	if (!bCrcMapBuilt)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGObjectGraphSubsystem: SceneCreateObjectByCrc for object %lld arrived before the CRC->actor-class map was built"), Msg.ObjectId);
		return;
	}

	TSubclassOf<AActor> ActorClass = ResolveActorClassForCrc(Msg.ObjectCrc);
	if (!ActorClass)
	{
		UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: CRC %08X (object %lld) resolves to no actor class — not spawning"), Msg.ObjectCrc, Msg.ObjectId);
		return;
	}


	// The player's own body resolves through the same SCOT->ASWGCreature mapping
	// as any NPC — upgrade to ASWGPlayer specifically for the ObjectId CmdStartScene
	// told us is "us." Only swaps a plain ASWGCreature; leaves other mappings alone.
	if (LocalPlayerObjectId != 0 && Msg.ObjectId == LocalPlayerObjectId && ActorClass == ASWGCreature::StaticClass())
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: Player Scene create object"));
		ActorClass = ASWGPlayer::StaticClass();
	}

	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGObjectGraphSubsystem: no World available to spawn object %lld"), Msg.ObjectId);
		return;
	}

	
	const FVector Location = SWGToUnrealSpace(FVector(Msg.PosX, Msg.PosY, Msg.PosZ));
	// SceneCreateObjectMessage's DirX/DirY/DirZ/DirW are the same left-handed
	// SWG quaternion FSWGWorldSnapshotReader::ReadNode decodes for static
	// placed objects (see its FQuat(QX, QZ, -QY, QW)): a plain Y/Z swap is a
	// reflection, not a proper rotation, so the surviving Y component must be
	// negated to preserve rotation sense instead of mirroring yaw.
	const FQuat Rotation(Msg.DirX, Msg.DirZ, -Msg.DirY, Msg.DirW);

	// Characters stand upright, so their server heading is effectively
	// yaw-only and whatever pitch/roll the quaternion decomposes to is noise.
	// Left in, it doesn't just tilt the mesh: APlayerController::OnPossess
	// overwrites ControlRotation with the pawn's actor rotation immediately
	// after PossessedBy returns, so a rolled spawn rotation rolls the entire
	// camera. Static objects (buildings, props, items) keep the full rotation
	// — they genuinely use it.
	FQuat SpawnRotation = Rotation;
	if (ActorClass->IsChildOf(ACharacter::StaticClass()))
	{
		SpawnRotation = FRotator(0.0f, Rotation.Rotator().Yaw, 0.0f).Quaternion();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* NewActor = World->SpawnActor<AActor>(ActorClass, FTransform(SpawnRotation, Location), SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGObjectGraphSubsystem: failed to spawn %s for object %lld"), *ActorClass->GetName(), Msg.ObjectId);
		return;
	}

	// Location is feet/ground-level (the network convention), but SpawnActor's
	// transform places the actor origin there — capsule center for an
	// ACharacter, not its bottom. Correct immediately; the actor is still
	// hidden until SceneEndBaselines so this is never visible mid-adjustment.
	const FVector Grounded = GroundedLocationFor(NewActor, Location);
	if (!Grounded.Equals(Location))
	{
		NewActor->SetActorLocation(Grounded);
	}

	if (ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(NewActor))
	{
		NetObject->SetObjectId(Msg.ObjectId);
		NetObject->SetObjectCrc(Msg.ObjectCrc);
	}

	// Hidden until SceneEndBaselines confirms the object is fully initialized.
	NewActor->SetActorHiddenInGame(true);
	NewActor->SetActorEnableCollision(false);

	ActorRegistry.Add(Msg.ObjectId, NewActor);

	// A registered handler (buildings, cells, ...) gets first refusal on
	// continuing this actor's generation; only fall back to the generic
	// one-mesh-component path when nothing is registered for its class. Note
	// this used to be a hardcoded allowlist of classes (ASWGCreature/Player/
	// Item/Building/Installation/StaticProp) — ASWGCell was missing from it,
	// so every cell spawned, registered, and just sat there invisible forever
	// with nothing ever finishing it. Always calling TryHandle means the next
	// class added to FSWGActorSpawnHandlerRegistry can't be missed the same way.
	FSWGActorSpawnArguments SpawnInfo{Msg.ObjectCrc, ActorClass };
	if (!FSWGActorSpawnHandlerRegistry::Get().TryHandle(*NewActor, SpawnInfo))
	{
		if (ActorClass->IsChildOf(ASWGCreature::StaticClass()) || ActorClass->IsChildOf(ASWGPlayer::StaticClass()) || ActorClass->IsChildOf(ASWGItem::StaticClass())
			|| ActorClass->IsChildOf(ASWGBuilding::StaticClass()) || ActorClass->IsChildOf(ASWGInstallation::StaticClass()) || ActorClass->IsChildOf(ASWGStaticProp::StaticClass()))
		{
			MeshGenerator->RequestMesh(NewActor, Msg.ObjectCrc);
		}
	}


	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: spawned %s for object %lld (crc %08X), registered"),
		*ActorClass->GetName(), Msg.ObjectId, Msg.ObjectCrc);
}

void USWGObjectGraphSubsystem::HandleBaselines(const FBaselinesMessage& Msg)
{
	AActor* Actor = FindActor(Msg.ObjectId);
	if (!Actor)
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: baseline for unknown object %lld (FourCC %s, slot %d)"),
			Msg.ObjectId, *Msg.GetObjectTypeFourCC(), Msg.BaselineType);
		return;
	}

	const FString FourCC = Msg.GetObjectTypeFourCC();

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: Baselines object=%lld FourCC=%s slot=%d actor=%s"),
		Msg.ObjectId, *FourCC, Msg.BaselineType, *Actor->GetName());

	FSWGBaselineArguments BaselineArgs{this};
	if (!FSWGBaselineHandlerRegistry::Get().TryHandle(*Actor, Msg, BaselineArgs))
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: no baseline dispatch for FourCC '%s' (object %lld, slot %d)"),
			*FourCC, Msg.ObjectId, Msg.BaselineType);
	}
}

void USWGObjectGraphSubsystem::HandleSceneEndBaselines(const FSceneEndBaselinesMessage& Msg)
{
	AActor* Actor = FindActor(Msg.ObjectId);
	if (!Actor)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGObjectGraphSubsystem: SceneEndBaselines for unknown object %lld"), Msg.ObjectId);
		return;
	}

	// A contained object (equipped gear, inventory contents) still goes
	// through the normal SceneCreateObjectByCrc/Baselines/SceneEndBaselines
	// flow — it just also gets an UpdateContainmentMessage with a nonzero
	if (const int64* ContainerId = ContainerByObjectId.Find(Msg.ObjectId); ContainerId && *ContainerId != 0)
	{
		ApplyContainment(Actor, *ContainerId);

		if (Actor->IsHidden())
		{
			UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: SceneEndBaselines object=%lld actor=%s — staying hidden (contained in %lld)"),
				Msg.ObjectId, *Actor->GetName(), *ContainerId);
			OnObjectReady.Broadcast(Msg.ObjectId);
			return;
		}
	}
	else
	{
		Actor->SetActorHiddenInGame(false);
		Actor->SetActorEnableCollision(true);
	}

	// The local player's own CREO — swap control from the editor's default
	// free-fly pawn to this one now that its position/orientation are final
	// (revealing any earlier is pointless: it's still hidden and its
	// transform may not reflect the server's actual baseline data yet).
	if (Msg.ObjectId == LocalPlayerObjectId)
	{
		if (UWorld* World = Actor->GetWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (APawn* PlayerPawn = Cast<APawn>(Actor))
				{
					PC->Possess(PlayerPawn);
					UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: possessed local player actor %lld (%s)"),
						Msg.ObjectId, *Actor->GetName());
				}
			}
		}
	}

	OnObjectReady.Broadcast(Msg.ObjectId);

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: SceneEndBaselines object=%lld actor=%s — revealed"),
		Msg.ObjectId, *Actor->GetName());

	// The local player's own CREO finishing baselines is the signal that the
	// zone is actually ready to look at — reveal the streaming level now
	// rather than waiting for some notion of "every object done," which
	// never really happens in an open world (NPCs keep streaming in as you move).
	if (LocalPlayerObjectId != 0 && Msg.ObjectId == LocalPlayerObjectId)
	{
		RevealCurrentZoneLevel();
	}
}

void USWGObjectGraphSubsystem::HandleUpdateContainment(const FUpdateContainmentMessage& Msg)
{
	ContainerByObjectId.Add(Msg.ObjectId, Msg.ContainerId);

	AActor* Actor = FindActor(Msg.ObjectId);

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: UpdateContainmentMessage object=%lld container=%lld type=%u actor=%s"),
		Msg.ObjectId, Msg.ContainerId, Msg.Type, Actor ? *Actor->GetName() : TEXT("<not spawned yet>"));

	// If the actor hasn't spawned yet (containment can arrive before its own
	// SceneCreateObjectByCrc), there's nothing to hide/show right now —
	// HandleSceneEndBaselines checks ContainerByObjectId itself once it does.
	// Cells are exempt: a cell's ContainerId is its owning building, not a
	// "tucked away, not visible" container like inventory/equipment — see
	// the matching exemption in HandleSceneEndBaselines.
	if (Actor && !Cast<ASWGCell>(Actor))
	{
		ApplyContainment(Actor, Msg.ContainerId);
	}

	// A cell's owning building is ContainerId here, but its cell number comes
	// from its own TLCS baseline (FSWGCellBaselineHandler), not
	// from Msg.Type — that's always -1 (VolumeContained) for a cell, same as
	// any other volume-contained object. FSWGCellSpawnHandler owns deciding
	// whether both pieces are known yet and actually finishing the cell.
	FSWGCellSpawnHandler::CheckAndFinishCell(*this, Msg.ObjectId, TreSubsystem, MeshGenerator);
}

void USWGObjectGraphSubsystem::HandleUpdateTransform(const FUpdateTransformMessage& Msg)
{
	AActor* Actor = FindActor(Msg.ObjectId);
	if (!Actor)
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: UpdateTransformMessage for unknown object %lld"), Msg.ObjectId);
		return;
	}

	// Same X,Z,Y wire order as the initial spawn position (SceneCreateObjectMessage)
	// — no axis swap needed, just direct field->component mapping. Msg.PosZ is
	// feet/ground-level; GroundedLocationFor corrects for ACharacter's capsule
	// center being the actual actor origin (see its own comment / the header's).
	// Raw wire position -> UE space at this boundary, same as the initial spawn.
	const FVector OldLocation = Actor->GetActorLocation();
	const FVector NewLocation = GroundedLocationFor(Actor, SWGToUnrealSpace(FVector(Msg.PosX, Msg.PosY, Msg.PosZ)));
	Actor->SetActorLocation(NewLocation);

	// This is a raw position teleport, not movement driven through the
	// character's own CharacterMovementComponent simulation (no
	// AddMovementInput, no physics step) — Velocity is never otherwise
	// touched for a network-driven actor, which left USWGMeshGeneratorSubsystem::
	// Tick()'s Character->GetVelocity() read (used to feed the locomotion
	// blend space) permanently zero even while visibly moving. Derive it
	// from the position delta since the last update instead; see
	// USWGMovementComponent::LastNetworkUpdateTime's comment for how
	// staleness (the creature stopped) gets handled on the read side.
	if (ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (USWGMovementComponent* Movement = Cast<USWGMovementComponent>(Character->GetCharacterMovement()))
		{
			const float CurrentTime = Actor->GetWorld()->GetTimeSeconds();
			if (Movement->LastNetworkUpdateTime > 0.0f)
			{
				const float DeltaTime = CurrentTime - Movement->LastNetworkUpdateTime;
				if (DeltaTime > KINDA_SMALL_NUMBER)
				{
					Movement->Velocity = (NewLocation - OldLocation) / DeltaTime;
				}
			}
			Movement->LastNetworkUpdateTime = CurrentTime;
		}
	}

	// DirectionAngle is a single byte (0-255) mapping to a full 0-360 degree
	// yaw — cheap per-tick facing without transmitting a full quaternion like
	// the initial spawn does. Pitch/Roll aren't part of this message (walking
	// creatures don't need them), so only Yaw changes here. Same left-handed
	// to right-handed conversion as the initial spawn quaternion (see
	// SceneCreateObjectByCrc's FQuat(Msg.DirX, Msg.DirZ, -Msg.DirY, Msg.DirW))
	// — for a pure yaw rotation that swap-with-negation reduces to simple
	// negation of the angle, not an additive offset.
	const float YawDegrees = -((Msg.DirectionAngle / 256.0f) * 360.0f);
	FRotator NewRotation = Actor->GetActorRotation();
	NewRotation.Yaw = YawDegrees;
	Actor->SetActorRotation(NewRotation);
}

void USWGObjectGraphSubsystem::HandleObjControllerMessage(const FObjControllerMessageIn& Msg)
{
	// This ObjectController-wrapped envelope is what GroundZoneComponent::teleport
	// pushes for zone-in and bounce-back corrections, both of which re-arm
	// PlayerObject::isTeleporting server-side. Acking unconditionally is a
	// harmless no-op when it wasn't actually re-armed.
	if (LocalPlayerObjectId != 0 && Msg.ObjectId == LocalPlayerObjectId && Network)
	{
		FTeleportAck Ack(LocalPlayerObjectId);
		Ack.MoveCount = 1;
		Network->SendMessage(Ack.Serialize());
	}
}

void USWGObjectGraphSubsystem::ApplyContainment(AActor* Actor, int64 ContainerId)
{
	if (!Actor)
	{
		return;
	}

	if (Actor->IsA<ASWGCell>())
	{
		Actor->SetActorHiddenInGame(false);
		Actor->SetActorEnableCollision(true);
		return;
	}

	AActor* ContainerActor = ContainerId != 0 ? FindActor(ContainerId) : nullptr;
	ASWGCell* ContainerCell = Cast<ASWGCell>(ContainerActor);
	const bool bContainedInCell = ContainerCell != nullptr;
	const bool bContained = ContainerId != 0 && !bContainedInCell;
	Actor->SetActorHiddenInGame(bContained);
	Actor->SetActorEnableCollision(!bContained);

	const bool bIsCreature = Actor->IsA<ASWGCreature>();

	if (bContainedInCell && !bIsCreature)
	{
		if (Actor->GetAttachParentActor() != ContainerCell)
		{
			Actor->AttachToActor(ContainerCell, FAttachmentTransformRules::KeepRelativeTransform);
		}
	}
	else if (!bContainedInCell && !bIsCreature && Actor->GetAttachParentActor() != nullptr)
	{
		Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
}

void USWGObjectGraphSubsystem::HandleDeltas(const FDeltasMessage& Msg)
{
	AActor* Actor = FindActor(Msg.ObjectId);
	if (!Actor)
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: delta for unknown object %lld (FourCC %s, slot %d)"),
			Msg.ObjectId, *Msg.GetObjectTypeFourCC(), Msg.DeltaType);
		return;
	}

	// Field-index application deferred — see world-object-plan.html "Delta application".
	UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: delta for object %lld type '%s' slot %d (%d update ops, not yet applied)"),
		Msg.ObjectId, *Msg.GetObjectTypeFourCC(), Msg.DeltaType, Msg.UpdateCount);
}
