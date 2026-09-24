

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
#include "Network/Messages/Zone/SceneDestroyObjectMessage.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/Zone/CmdStartSceneMessage.h"
#include "Network/Messages/Zone/UpdateContainmentMessage.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"
#include "Network/Messages/Zone/UpdateTransformMessage.h"
#include "Network/Messages/Zone/UpdateTransformWithParentMessage.h"
#include "Network/Messages/Zone/ObjControllerMessageIn.h"
#include "Network/Messages/Zone/Object/TeleportAck.h"
#include "Network/Messages/Zone/Object/PostureUpdateIn.h"

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
#include "Subsystems/SWGDeltaHandlerRegistry.h"
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

TArray<int64> USWGObjectGraphSubsystem::FindContainedObjectIds(int64 ContainerId) const
{
	TArray<int64> Contained;
	for (const TPair<int64, int64>& Pair : ContainerByObjectId)
	{
		if (Pair.Value == ContainerId)
		{
			Contained.Add(Pair.Key);
		}
	}
	return Contained;
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
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::SceneDestroyObject))
	{
		HandleSceneDestroyObject(*static_cast<const FSceneDestroyObjectMessage*>(Msg.Get()));
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
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::UpdateTransformMessageWithParent))
	{
		HandleUpdateTransformWithParent(*static_cast<const FUpdateTransformWithParentMessage*>(Msg.Get()));
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
	PlayerObjectId = 0;
	bRevealPendingPlayerPlacement = false;

	// FSWGZoneLoadingState reopens the level and the server resends everything
	// from scratch; a stale containment here would misplace a reused id.
	ActorRegistry.Reset();
	ObjectCrcById.Reset();
	ContainerByObjectId.Reset();
	ContainmentTypeByObjectId.Reset();
	CellNumberByObjectId.Reset();
	ReadyObjects.Reset();
	PendingMessages.Reset();

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

	// Recorded unconditionally — an object that resolves to no actor class
	// below (ITNO/intangible, mainly) still needs this CRC remembered
	// somewhere, since nothing else client-side ever sees it again otherwise.
	ObjectCrcById.Add(Msg.ObjectId, Msg.ObjectCrc);

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
	// DirX/DirY/DirZ/DirW are the native wire quaternion, the same one
	// FSWGWorldSnapshotReader::ReadNode decodes for static placed objects.
	const FQuat Rotation = SWGNativeToUnrealRotation(Msg.DirX, Msg.DirY, Msg.DirZ, Msg.DirW);

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

	// Whether that position was world or cell-relative isn't known until the
	// containment arrives — see ApplyContainment.
	if (ASWGCreature* Creature = Cast<ASWGCreature>(NewActor))
	{
		Creature->bAwaitingCellPlacement = true;
	}
	else if (ASWGObject* Object = Cast<ASWGObject>(NewActor))
	{
		Object->bAwaitingCellPlacement = true;
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
			ApplyClientDataFile(NewActor, Msg.ObjectCrc);
			MeshGenerator->RequestMesh(NewActor, Msg.ObjectCrc);
		}
	}

	// CREO4's TurnScale only multiplies the template's own turnRate.
	if (ASWGCreature* Creature = Cast<ASWGCreature>(NewActor); Creature && TreSubsystem)
	{
		const FString TemplatePath = TreSubsystem->ResolveTemplatePath(Msg.ObjectCrc);
		float RunTurnRate = 0.0f;
		float WalkTurnRate = 0.0f;
		USWGMovementComponent* Movement = Creature->GetSWGMovementComponent();
		if (Movement
			&& TreSubsystem->FindTemplateCreatureFloat(TemplatePath, TEXT("turnRate"), 0, RunTurnRate)
			&& TreSubsystem->FindTemplateCreatureFloat(TemplatePath, TEXT("turnRate"), 1, WalkTurnRate))
		{
			Movement->SetTemplateTurnRates(RunTurnRate, WalkTurnRate);
		}
	}


	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: spawned %s for object %lld (crc %08X), registered"),
		*ActorClass->GetName(), Msg.ObjectId, Msg.ObjectCrc);
}

void USWGObjectGraphSubsystem::HandleBaselines(const FBaselinesMessage& Msg)
{
	AActor* Actor = ResolveMessageActor(Msg.ObjectId, Msg.GetObjectType());
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
	ReadyObjects.Add(Msg.ObjectId);

	const bool bIsLocalPlayer = LocalPlayerObjectId != 0 && Msg.ObjectId == LocalPlayerObjectId;

	if (const int64* ContainerId = ContainerByObjectId.Find(Msg.ObjectId); ContainerId && *ContainerId != 0)
	{
		ApplyContainment(Actor, Msg.ObjectId, *ContainerId);
		SyncSlottedEquipment(Msg.ObjectId, 0);

		// The local player is never tucked away — if its cell is unknown it
		// zoned in inside a snapshot building that hasn't loaded yet. Take
		// control now; ApplyContainment reveals the level once it's placed.
		if (bIsLocalPlayer)
		{
			if (Actor->IsHidden())
			{
				Actor->SetActorHiddenInGame(false);
				Actor->SetActorEnableCollision(true);
			}
			if (ASWGCreature* Creature = Cast<ASWGCreature>(Actor); Creature && Creature->bAwaitingCellPlacement)
			{
				bRevealPendingPlayerPlacement = true;
				Creature->GetCharacterMovement()->DisableMovement();
			}
		}

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

		// Spawned in world space, so a cell entered later must not re-place it.
		// Needed for the local player, which never gets an UpdateTransform.
		if (ASWGCreature* Creature = Cast<ASWGCreature>(Actor))
		{
			Creature->bAwaitingCellPlacement = false;
		}
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
	if (bIsLocalPlayer && !bRevealPendingPlayerPlacement)
	{
		RevealCurrentZoneLevel();
	}
}

void USWGObjectGraphSubsystem::HandleUpdateContainment(const FUpdateContainmentMessage& Msg)
{
	const int64 PreviousContainerId = ContainerByObjectId.FindRef(Msg.ObjectId);
	ContainerByObjectId.Add(Msg.ObjectId, Msg.ContainerId);
	ContainmentTypeByObjectId.Add(Msg.ObjectId, (int32)Msg.Type);

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
		ApplyContainment(Actor, Msg.ObjectId, Msg.ContainerId);
	}

	// Core3 links before it sends baselines, so an item's first containment
	// lands before its TANO3 — HandleSceneEndBaselines syncs that one. This
	// covers an NPC or player changing gear later.
	if (Actor && ReadyObjects.Contains(Msg.ObjectId))
	{
		SyncSlottedEquipment(Msg.ObjectId, PreviousContainerId);
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

	// Same raw (Core3) order as the initial spawn position. Msg.PosZ is
	// feet/ground-level; GroundedLocationFor corrects for ACharacter's capsule
	// center being the actual actor origin (see its own comment / the header's).
	// Raw wire position -> UE space at this boundary, same as the initial spawn.
	const FVector NewLocation = GroundedLocationFor(Actor, SWGToUnrealSpace(FVector(Msg.PosX, Msg.PosY, Msg.PosZ)));

	// This message is world space, so the transform no longer needs composing into a cell.
	if (ASWGCreature* Creature = Cast<ASWGCreature>(Actor))
	{
		Creature->bAwaitingCellPlacement = false;
		Creature->PlacedInCell = nullptr;
	}

	// DirectionAngle is Quaternion::getSpecialDegrees() — a full turn is 100,
	// not 256. Pitch/Roll aren't part of this message, so only Yaw changes
	// here. SWG's yaw about its up axis maps 1:1 onto UE yaw (see
	// SWGWorldScale.h), so the heading is used as-is.
	const float YawDegrees = (Msg.DirectionAngle / 100.0f) * 360.0f;

	ApplyNetworkTransform(Actor, Msg.ObjectId, NewLocation, YawDegrees);
}

void USWGObjectGraphSubsystem::HandleUpdateTransformWithParent(const FUpdateTransformWithParentMessage& Msg)
{
	AActor* Actor = FindActor(Msg.ObjectId);
	if (!Actor)
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: UpdateTransformWithParentMessage for unknown object %lld"), Msg.ObjectId);
		return;
	}

	// Cell-relative, which is building-relative — the same composition
	// ApplyContainment does once for a creature spawned inside.
	ASWGCell* Cell = Cast<ASWGCell>(FindActor(Msg.ParentId));
	ASWGBuilding* Building = Cell ? Cell->OwningBuilding.Get() : nullptr;
	const FVector RelativeLocation = SWGToUnrealSpace(FVector(Msg.PosX, Msg.PosY, Msg.PosZ));
	const float RelativeYaw = (Msg.DirectionAngle / 100.0f) * 360.0f;
	ASWGCreature* Creature = Cast<ASWGCreature>(Actor);

	if (!Building)
	{
		// The room isn't finished (or the cell hasn't arrived): park the
		// relative transform on the actor and let ApplyContainment compose it
		// when NotifyCellFinished fires, exactly like a spawn inside a cell.
		if (Creature)
		{
			Creature->bAwaitingCellPlacement = true;
		}
		Actor->SetActorLocation(GroundedLocationFor(Actor, RelativeLocation));
		FRotator Rotation = Actor->GetActorRotation();
		Rotation.Yaw = RelativeYaw;
		Actor->SetActorRotation(Rotation);
		return;
	}

	const FTransform& BuildingTransform = Building->GetActorTransform();
	const FVector NewLocation = GroundedLocationFor(Actor, BuildingTransform.TransformPosition(RelativeLocation));
	const float YawDegrees = FRotator::NormalizeAxis(BuildingTransform.Rotator().Yaw + RelativeYaw);

	if (Creature)
	{
		Creature->bAwaitingCellPlacement = false;
		Creature->PlacedInCell = Cell;
	}

	ApplyNetworkTransform(Actor, Msg.ObjectId, NewLocation, YawDegrees);
}

void USWGObjectGraphSubsystem::ApplyNetworkTransform(AActor* Actor, int64 ObjectId, const FVector& NewLocation, float YawDegrees)
{
	const FVector OldLocation = Actor->GetActorLocation();
	ACharacter* Character = Cast<ACharacter>(Actor);
	USWGMovementComponent* Movement = Character ? Cast<USWGMovementComponent>(Character->GetCharacterMovement()) : nullptr;

	// Updates arrive a few times a second, so applying them directly makes
	// creatures jump between positions. Hand the destination to the movement
	// component to walk toward instead (USWGMovementComponent::
	// TickNetworkSmoothing), which also gives the blend space a real Velocity
	// to read. Three cases still land immediately: static objects with no
	// movement component, the client-authoritative local pawn, and a jump too
	// far to walk — a teleport or zone-in rather than locomotion.
	const bool bIsLocalPlayer = ObjectId == LocalPlayerObjectId;
	const bool bTeleport = FVector::Dist2D(OldLocation, NewLocation) > MaxSmoothedMoveDistance;

	// The mount the local player is driving is client-authoritative too, and
	// the server echoes its (always slightly stale) position back to us as
	// the rider — smoothing toward that would drag the vehicle backwards.
	// Only a real jump (server correction, zone move) is honoured.
	const ASWGCreature* LocalPlayer = Cast<ASWGCreature>(FindActor(LocalPlayerObjectId));
	if (LocalPlayer && LocalPlayer->RiddenMount.Get() == Actor && !bTeleport)
	{
		return;
	}

	if (Movement && !bIsLocalPlayer && !bTeleport)
	{
		Movement->SetNetworkTarget(NewLocation, YawDegrees);
	}
	else
	{
		Actor->SetActorLocation(NewLocation);

		FRotator NewRotation = Actor->GetActorRotation();
		NewRotation.Yaw = YawDegrees;
		Actor->SetActorRotation(NewRotation);

		if (Movement)
		{
			Movement->ClearNetworkTarget();
			Movement->Velocity = FVector::ZeroVector;
		}
	}

	if (Movement)
	{
		Movement->LastNetworkUpdateTime = Actor->GetWorld()->GetTimeSeconds();
	}
}

void USWGObjectGraphSubsystem::HandleObjControllerMessage(const FObjControllerMessageIn& Msg)
{
	// Addressed to whichever creature changed posture, not just to us —
	// a dying NPC's arrives with its own ObjectId.
	if (Msg.GetSubOp() == ESWGObjControllerOp::PostureUpdate)
	{
		FSWGPacket Payload = Msg.AsPayloadPacket();
		FPostureUpdateIn Update;
		if (!Update.Parse(Payload))
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGObjectGraphSubsystem: malformed PostureUpdate for %lld (%d payload bytes)"), Msg.ObjectId, Msg.RawPayload.Num());
			return;
		}

		if (USWGCombatStateComponent* CombatState = FindComponent<USWGCombatStateComponent>(Msg.ObjectId))
		{
			UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: PostureUpdate %lld -> posture %u"), Msg.ObjectId, Update.Posture);
			CombatState->ApplyPostureUpdate(Update.Posture);
		}
		return;
	}

	// A server-pushed DataTransform (WithParent when we're in a cell) is
	// zone-in or a bounce-back correction, both of which re-arm
	// PlayerObject::isTeleporting; acking one that didn't is a harmless no-op.
	// The sub-op test matters because CombatAction, CombatSpam and
	// CommandQueueRemove all arrive in this envelope addressed to us, and
	// acking those floods the wire once combat starts.
	if (Msg.GetSubOp() != ESWGObjControllerOp::DataTransform && Msg.GetSubOp() != ESWGObjControllerOp::DataTransformWithParent)
	{
		return;
	}

	if (LocalPlayerObjectId != 0 && Msg.ObjectId == LocalPlayerObjectId && Network)
	{
		FTeleportAck Ack(LocalPlayerObjectId);
		// TeleportAckCallback discards this, but echoing beats inventing.
		Ack.MoveCount = Msg.TickCount;
		Network->SendMessage(Ack.Serialize());
	}
}

void USWGObjectGraphSubsystem::ApplyContainment(AActor* Actor, int64 ObjectId, int64 ContainerId)
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
	const bool bIsCreature = Actor->IsA<ASWGCreature>();

	// A creature contained (Rider) in another creature is being mounted, not
	// tucked into a bag — attach it to the mount's seat instead of hiding it.
	// Rider shares its wire value (4) with "equipped in arrangement group 0"
	// (see ESWGContainmentType's doc comment), but that ambiguity can't
	// misfire here: an equipped item's actor is an ASWGItem, never an
	// ASWGCreature, so bIsCreature already rules ordinary gear out.
	ASWGCreature* MountActor = (bIsCreature && ContainerActor != Actor) ? Cast<ASWGCreature>(ContainerActor) : nullptr;
	const bool bMounting = MountActor != nullptr
		&& ContainmentTypeByObjectId.FindRef(ObjectId) == static_cast<int32>(ESWGContainmentType::Rider);

	if (bMounting)
	{
		ApplyRiderContainment(CastChecked<ASWGCreature>(Actor), MountActor);
		return;
	}

	// Not (or no longer) a Rider containment — a creature that was mounted
	// needs to be put back on its own feet before falling through to the
	// generic hide/attach handling below (e.g. dismounting into a cell, or
	// straight into the world).
	if (bIsCreature)
	{
		if (ASWGCreature* Creature = CastChecked<ASWGCreature>(Actor); Creature->RiddenMount.IsValid())
		{
			ApplyRiderContainment(Creature, nullptr);
		}
	}

	Actor->SetActorHiddenInGame(bContained);
	Actor->SetActorEnableCollision(!bContained);

	if (bContainedInCell && !bIsCreature)
	{
		if (Actor->GetAttachParentActor() != ContainerCell)
		{
			// First attach: the spawn transform is cell-relative. Any later
			// one (a streamed-out cell detached this with its world transform,
			// or the server moved it between rooms) keeps world.
			ASWGObject* Object = Cast<ASWGObject>(Actor);
			const bool bRelative = Object && Object->bAwaitingCellPlacement;
			if (Object)
			{
				Object->bAwaitingCellPlacement = false;
			}
			Actor->AttachToActor(ContainerCell, bRelative ? FAttachmentTransformRules::KeepRelativeTransform : FAttachmentTransformRules::KeepWorldTransform);
		}
	}
	else if (!bContainedInCell && !bIsCreature && Actor->GetAttachParentActor() != nullptr)
	{
		Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}

	// Creatures aren't attached (a character's movement component doesn't
	// cooperate with a parent), so the cell-relative spawn transform is
	// composed into world space once instead — see bAwaitingCellPlacement.
	if (bContainedInCell && bIsCreature)
	{
		ASWGCreature* Creature = CastChecked<ASWGCreature>(Actor);

		// A network cell whose room hasn't streamed yet exists but sits at
		// the origin, unattached — composing against it put a zoning-in
		// player at cell-local coordinates in the sky. Wait for
		// NotifyCellFinished; the local player's room is forced through
		// FSWGCellSpawnHandler::FinishCell so this never stalls.
		if (Creature->bAwaitingCellPlacement
			&& (!ContainerCell->OwningBuilding.IsValid() || !ContainerCell->bCollisionReady))
		{
			UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: %s waits for cell %lld collision before placement"), *Actor->GetName(), ContainerId);
			return;
		}

		if (Creature->bAwaitingCellPlacement)
		{
			Creature->bAwaitingCellPlacement = false;
			Creature->PlacedInCell = ContainerCell;

			// X/Y are untouched since spawn (no transform updates reach an
			// in-cell creature), but Z may have fallen under gravity while the
			// cell was unknown — rebuild it from the stored network Z.
			const float HalfHeight = Creature->GetCapsuleComponent() ? Creature->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f;
			FTransform Relative = Actor->GetActorTransform();
			Relative.SetLocation(FVector(Relative.GetLocation().X, Relative.GetLocation().Y, Creature->LastNetworkZ + HalfHeight));
			Creature->SetActorTransform(Relative * ContainerCell->GetActorTransform());

			// Feet-level, world space — the capsule resize in
			// USWGMeshGeneratorSubsystem::BuildGeneratedMeshComponent reads this
			// back when the mesh lands, which can be after this.
			Creature->LastNetworkZ = Creature->GetActorLocation().Z - HalfHeight;
			if (Creature->GetObjectId() == LocalPlayerObjectId)
			{
				Creature->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			}

			// The local player zoned in inside a building: the level was held
			// back until it stood somewhere real (HandleSceneEndBaselines).
			if (bRevealPendingPlayerPlacement && Creature->GetObjectId() == LocalPlayerObjectId)
			{
				bRevealPendingPlayerPlacement = false;
				RevealCurrentZoneLevel();
			}
		}
	}
}

void USWGObjectGraphSubsystem::ApplyRiderContainment(ASWGCreature* Rider, ASWGCreature* Mount)
{
	if (!Rider)
	{
		return;
	}

	if (!Mount)
	{
		// Dismounting: undo the attach and give the rider its own legs back.
		Rider->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Rider->SetActorEnableCollision(true);
		if (USWGMovementComponent* RiderMovement = Rider->GetSWGMovementComponent())
		{
			RiderMovement->SetMovementMode(MOVE_Walking);
		}
		// Hand the vehicle back to the network: it's parked, and the server's
		// transforms for it apply again (see ApplyNetworkTransform).
		if (ASWGCreature* PreviousMount = Rider->RiddenMount.Get())
		{
			if (USWGMovementComponent* MountMovement = PreviousMount->GetSWGMovementComponent())
			{
				MountMovement->bRunPhysicsWithNoController = false;
				MountMovement->StopMovementImmediately();
			}
		}
		UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: %s dismounted"), *Rider->GetName());
		Rider->RiddenMount.Reset();
		return;
	}

	// No slot_definitions.iff row exists for a rider seat (unlike ordinary
	// equip hardpoints — see USWGEquipmentComponent::AttachMeshToHardpoint),
	// so there's no data-driven socket name to look up for an ordinary
	// creature mount. A vehicle's own body mesh carries a real "player"
	// hardpoint instead (see USWGMeshGeneratorSubsystem::TryAttachVehicleBody)
	// — used below when present. Otherwise fall back to the mesh root so
	// mounting still works, just without a proper seat offset.
	static const FName RiderSocketName(TEXT("rider"));
	USkeletalMeshComponent* MountMesh = Mount->GetMesh();
	const bool bHasSeatSocket = MountMesh && MountMesh->DoesSocketExist(RiderSocketName);
	if (!bHasSeatSocket && !Mount->RiderSeatTransform.IsSet())
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGObjectGraphSubsystem: mount %s has no '%s' socket or body hardpoint — attaching rider %s at its mesh root"),
			*Mount->GetName(), *RiderSocketName.ToString(), *Rider->GetName());
	}

	// A Character's movement component fights a parent attachment (see the
	// "Creatures aren't attached" comment above for cell placement), so
	// movement is switched off before attaching rather than left to contest it.
	if (USWGMovementComponent* RiderMovement = Rider->GetSWGMovementComponent())
	{
		RiderMovement->SetMovementMode(MOVE_None);
	}
	Rider->SetActorEnableCollision(false);
	Rider->SetActorHiddenInGame(false);

	// The local player drives its mount client-side, like it does itself:
	// ASWGPlayer::Move feeds it AddMovementInput without possessing it, and
	// UCharacterMovementComponent only simulates a controller-less character
	// when bRunPhysicsWithNoController is set — otherwise it consumes the
	// input and discards it. Any pending network target would also pre-empt
	// the simulation (USWGMovementComponent::TickComponent). Someone else's
	// mount stays network-driven.
	if (Rider->GetObjectId() == LocalPlayerObjectId)
	{
		if (USWGMovementComponent* MountMovement = Mount->GetSWGMovementComponent())
		{
			MountMovement->ClearNetworkTarget();
			MountMovement->bRunPhysicsWithNoController = true;
			MountMovement->SetMovementMode(MOVE_Walking);
		}
	}

	if (bHasSeatSocket)
	{
		Rider->AttachToComponent(MountMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, RiderSocketName);
	}
	else if (Mount->RiderSeatTransform.IsSet())
	{
		Rider->AttachToComponent(Mount->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		Rider->SetActorRelativeTransform(*Mount->RiderSeatTransform);
	}
	else if (USceneComponent* AttachTarget = MountMesh ? static_cast<USceneComponent*>(MountMesh) : Mount->GetRootComponent())
	{
		Rider->AttachToComponent(AttachTarget, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}

	Rider->RiddenMount = Mount;

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: %s mounted %s"), *Rider->GetName(), *Mount->GetName());
}

void USWGObjectGraphSubsystem::SyncSlottedEquipment(int64 ObjectId, int64 PreviousContainerId)
{
	const int64 ContainerId = ContainerByObjectId.FindRef(ObjectId);
	const int32 ContainmentType = ContainmentTypeByObjectId.FindRef(ObjectId);
	const bool bSlotted = ContainerId != 0 && SWGIsSlottedArrangement(ContainmentType);

	if (PreviousContainerId != 0 && (PreviousContainerId != ContainerId || !bSlotted))
	{
		if (ASWGCreature* PreviousCreature = Cast<ASWGCreature>(FindActor(PreviousContainerId)))
		{
			PreviousCreature->EquipmentComponent->RemoveContainedItem((uint64)ObjectId);
		}
	}

	if (!bSlotted)
	{
		return;
	}

	ASWGCreature* Creature = Cast<ASWGCreature>(FindActor(ContainerId));
	ASWGItem* Item = Cast<ASWGItem>(FindActor(ObjectId));
	if (!Creature || !Item)
	{
		return;
	}

	FEquiptmentItem Equipment;
	Equipment.ObjectId = (uint64)ObjectId;
	Equipment.TemplateCRC = Item->GetObjectCrc();
	Equipment.ContainmentType = ContainmentType;
	if (Item->TangibleComponent)
	{
		Equipment.CustomizationBytes = Item->TangibleComponent->CustomizationBytes;
	}
	Creature->EquipmentComponent->SetContainedItem(Equipment);
}

void USWGObjectGraphSubsystem::ApplyClientDataFile(AActor* Actor, uint32 TemplateCrc)
{
	ASWGCreature* Creature = Cast<ASWGCreature>(Actor);
	if (!Creature || !MeshGenerator)
	{
		return;
	}

	FSWGClientDataFile ClientData;
	if (!MeshGenerator->ResolveClientDataFile(TemplateCrc, ClientData))
	{
		return;
	}

	if (Creature->TangibleComponent && !ClientData.Customization.IsEmpty())
	{
		Creature->TangibleComponent->ClientDataCustomization = MeshGenerator->ToCustomizationVariables(ClientData.Customization);
	}

	if (Creature->EquipmentComponent && !ClientData.Wearables.IsEmpty())
	{
		Creature->EquipmentComponent->SetClientDataWearables(ClientData.Wearables);
	}

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: %s (crc %08X) client data: %d wearable group(s), %d body customization value(s)"),
		*Actor->GetName(), TemplateCrc, ClientData.Wearables.Num(), ClientData.Customization.Num());
}

void USWGObjectGraphSubsystem::UnregisterStaticObject(int64 ObjectId)
{
	ActorRegistry.Remove(ObjectId);
	ContainerByObjectId.Remove(ObjectId);
	CellNumberByObjectId.Remove(ObjectId);
	ReadyObjects.Remove(ObjectId);
}

void USWGObjectGraphSubsystem::RegisterStaticObject(int64 ObjectId, AActor* Actor, int64 ContainerId)
{
	if (!Actor || ObjectId == 0)
	{
		return;
	}

	if (ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(Actor))
	{
		NetObject->SetObjectId(ObjectId);
	}

	ActorRegistry.Add(ObjectId, Actor);
	ReadyObjects.Add(ObjectId);
	if (ContainerId != 0)
	{
		ContainerByObjectId.Add(ObjectId, ContainerId);
	}

	// Snapshot objects spawn after the async terrain load, well after the
	// first wave of scene messages — anything already sitting in this cell
	// was hidden as "contained in something unknown" by HandleUpdateContainment.
	if (Actor->IsA<ASWGCell>())
	{
		for (const TPair<int64, int64>& Pair : ContainerByObjectId)
		{
			if (Pair.Value == ObjectId && ReadyObjects.Contains(Pair.Key))
			{
				ApplyContainment(FindActor(Pair.Key), Pair.Key, ObjectId);
			}
		}
	}
}

void USWGObjectGraphSubsystem::NotifyCellFinished(int64 CellObjectId)
{
	if (CellObjectId == 0)
	{
		return;
	}
	for (const TPair<int64, int64>& Pair : ContainerByObjectId)
	{
		if (Pair.Value == CellObjectId && ReadyObjects.Contains(Pair.Key))
		{
			ApplyContainment(FindActor(Pair.Key), Pair.Key, CellObjectId);
		}
	}
}

bool USWGObjectGraphSubsystem::IsLocalPlayerContainedIn(int64 ContainerId) const
{
	return LocalPlayerObjectId != 0 && ContainerId != 0 && ContainerByObjectId.FindRef(LocalPlayerObjectId) == ContainerId;
}

bool USWGObjectGraphSubsystem::IsOwnedByLocalPlayer(int64 ObjectId) const
{
	if (LocalPlayerObjectId == 0 || ObjectId == 0)
	{
		return false;
	}

	int64 Current = ObjectId;
	constexpr int32 MaxHops = 16;
	for (int32 Hop = 0; Hop < MaxHops; ++Hop)
	{
		const int64* ContainerId = ContainerByObjectId.Find(Current);
		if (!ContainerId || *ContainerId == 0)
		{
			return false;
		}
		if (*ContainerId == LocalPlayerObjectId)
		{
			return true;
		}
		Current = *ContainerId;
	}
	return false;
}

AActor* USWGObjectGraphSubsystem::ResolveMessageActor(int64 ObjectId, ESWGObjectType ObjectType)
{
	return ObjectType == ESWGObjectType::PLAY ? ResolvePlayerObjectActor(ObjectId) : FindActor(ObjectId);
}

AActor* USWGObjectGraphSubsystem::ResolvePlayerObjectActor(int64 ObjectId)
{
	if (AActor* Registered = FindActor(ObjectId))
	{
		return Registered;
	}

	const int64* ContainerId = ContainerByObjectId.Find(ObjectId);
	AActor* OwningActor = ContainerId ? FindActor(*ContainerId) : nullptr;
	if (!OwningActor)
	{
		return nullptr;
	}

	// Registered so later messages for it resolve through the normal lookup.
	ActorRegistry.Add(ObjectId, OwningActor);

	// Only the local player's own shares an actor that must survive a destroy.
	if (LocalPlayerObjectId != 0 && *ContainerId == LocalPlayerObjectId)
	{
		PlayerObjectId = ObjectId;
	}

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: routing player object %lld to %s (container %lld)"),
		ObjectId, *OwningActor->GetName(), *ContainerId);
	return OwningActor;
}

void USWGObjectGraphSubsystem::HandleSceneDestroyObject(const FSceneDestroyObjectMessage& Msg)
{
	RemoveObject(Msg.ObjectId);
}

void USWGObjectGraphSubsystem::RemoveObject(int64 ObjectId)
{
	TWeakObjectPtr<AActor> Registered;
	if (!ActorRegistry.RemoveAndCopyValue(ObjectId, Registered))
	{
		// Destroys arrive for objects that never spawned — an unresolved CRC, or
		// one that left view before its SceneCreateObjectByCrc was processed.
		UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: destroy for unregistered object %lld"), ObjectId);
		ContainerByObjectId.Remove(ObjectId);
		ContainmentTypeByObjectId.Remove(ObjectId);
		CellNumberByObjectId.Remove(ObjectId);
		ReadyObjects.Remove(ObjectId);
		return;
	}

	// An equipped item leaving view is a destroy, not an unequip containment —
	// take it off its creature before forgetting where it was.
	int64 PreviousContainerId = 0;
	ContainerByObjectId.RemoveAndCopyValue(ObjectId, PreviousContainerId);
	ContainmentTypeByObjectId.Remove(ObjectId);
	CellNumberByObjectId.Remove(ObjectId);
	ReadyObjects.Remove(ObjectId);
	SyncSlottedEquipment(ObjectId, PreviousContainerId);

	OnObjectDestroyed.Broadcast(ObjectId);

	AActor* Actor = Registered.Get();
	if (!Actor)
	{
		return;
	}

	// The local player's actor is possessed by the player controller; tearing it
	// out from under the controller mid-session leaves the client with no pawn.
	// Logout and zone changes destroy it through the level teardown instead.
	// The player object shares that actor, so a destroy for it must not take the
	// actor with it either.
	if (ObjectId != 0 && (ObjectId == LocalPlayerObjectId || ObjectId == PlayerObjectId))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGObjectGraphSubsystem: destroy for the local player object %lld — unregistered, actor left alone"), ObjectId);
		return;
	}

	// Buildings own their cells and doors by attachment, and those carry their
	// own registry entries. Destroy takes the children with it so nothing is
	// left parented to a destroyed actor if the server doesn't send a destroy
	// for each one.
	TArray<AActor*> Attached;
	Actor->GetAttachedActors(Attached, /*bResetArray*/ true, /*bRecursivelyIncludeAttachedActors*/ true);

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: destroying object %lld (%s) and %d attached actor(s)"),
		ObjectId, *Actor->GetName(), Attached.Num());

	for (AActor* Child : Attached)
	{
		if (!Child)
		{
			continue;
		}

		// Drop the child's own registry entry so it can't be looked up after this.
		if (const ISWGNetworkObjectInterface* NetworkObject = Cast<ISWGNetworkObjectInterface>(Child))
		{
			const int64 ChildId = NetworkObject->GetObjectId();
			ActorRegistry.Remove(ChildId);
			ContainerByObjectId.Remove(ChildId);
			ContainmentTypeByObjectId.Remove(ChildId);
			CellNumberByObjectId.Remove(ChildId);
			ReadyObjects.Remove(ChildId);
			OnObjectDestroyed.Broadcast(ChildId);
		}

		Child->Destroy();
	}

	Actor->Destroy();
}

void USWGObjectGraphSubsystem::HandleDeltas(const FDeltasMessage& Msg)
{
	AActor* Actor = ResolveMessageActor(Msg.ObjectId, Msg.GetObjectType());
	if (!Actor)
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: delta for unknown object %lld (FourCC %s, slot %d)"),
			Msg.ObjectId, *Msg.GetObjectTypeFourCC(), Msg.DeltaType);
		return;
	}

	FSWGDeltaArguments DeltaArgs{this};
	if (!FSWGDeltaHandlerRegistry::Get().TryHandle(*Actor, Msg, DeltaArgs))
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: delta for object %lld type '%s' slot %d (%d update ops, not yet applied)"),
			Msg.ObjectId, *Msg.GetObjectTypeFourCC(), Msg.DeltaType, Msg.UpdateCount);
	}
}
