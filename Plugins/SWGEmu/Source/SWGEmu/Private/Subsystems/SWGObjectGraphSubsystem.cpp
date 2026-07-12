

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

#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Objects/Creature/SWGCreature.h"

#include "Objects/SWGNetworkObjectInterface.h"
#include "Objects/Tangible/SWGItem.h"
#include "Objects/Creature/SWGCreature.h"
#include "Objects/Player/SWGPlayer.h"

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

namespace
{
	// Dispatch order below is byte-exact against each free-function parser
	// (SWGTangibleBaselineParser / SWGCreatureBaselineParser — see
	// world-object-plan.html "Component breakdown" / "Delta application").
	// Several slots interleave components mid-stream (e.g. CREO base3 is
	// Tangible/Condition, then CombatState, then loose actor fields, then
	// Health, then CombatState again, then Health again) — each component's
	// ApplyBaseX is split into ApplyBaseXPartN sub-calls wherever that happens,
	// called here in the exact wire order.

	// TANO base3: SWGTangibleBaselineParser::ParseBase3.
	void ApplyTangibleBaseline(ASWGItem& Item, uint8 Slot, FSWGPacket& Packet)
	{
		switch (Slot)
		{
			case 3:
				if (Item.TangibleComponent)
				{
					Item.TangibleComponent->ApplyBase3Part1(Packet);
				}
				if (Item.ConditionComponent)
				{
					Item.ConditionComponent->ApplyBase3(Packet);
				}
				if (Item.TangibleComponent)
				{
					Item.TangibleComponent->ApplyBase3Part2(Packet);
				}
				break;
			case 6:
				if (Item.DefenderComponent)
				{
					Item.DefenderComponent->ApplyBase6(Packet);
				}
				break;
			default:
				UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: no TANO baseline dispatch for slot %d"), Slot);
				break;
		}
	}

	// CREO base1/3/4/6: SWGCreatureBaselineParser::ParseBase1/3/4/6. "Loose"
	// fields (BankCredits/CashCredits/CreatureLinkId/Height/Level/GuildId) live
	// directly on ASWGCreature — see world-object-plan.html "Component
	// breakdown" — so they're read inline here rather than via a component.
	void ApplyCreatureBaseline(ASWGCreature& Creature, uint8 Slot, FSWGPacket& Packet)
	{
		switch (Slot)
		{
			case 1:
				Creature.BankCredits = Packet.ReadInt32();
				Creature.CashCredits = Packet.ReadInt32();
				if (Creature.HealthComponent)
				{
					Creature.HealthComponent->ApplyBase1(Packet);
				}
				if (Creature.SkillComponent)
				{
					Creature.SkillComponent->ApplyBase1(Packet);
				}
				break;
			case 3:
				// TangibleObjectMessage3 fields come first on the wire.
				if (Creature.TangibleComponent)
				{
					Creature.TangibleComponent->ApplyBase3Part1(Packet);
				}
				if (Creature.ConditionComponent)
				{
					Creature.ConditionComponent->ApplyBase3(Packet);
				}
				if (Creature.TangibleComponent)
				{
					Creature.TangibleComponent->ApplyBase3Part2(Packet);
				}
				if (Creature.CombatStateComponent)
				{
					Creature.CombatStateComponent->ApplyBase3Part1(Packet);
				}
				Creature.CreatureLinkId = Packet.ReadInt64();
				Creature.Height = Packet.ReadFloat();
				if (Creature.HealthComponent)
				{
					Creature.HealthComponent->ApplyBase3Part1(Packet);
				}
				if (Creature.CombatStateComponent)
				{
					Creature.CombatStateComponent->ApplyBase3Part2(Packet);
				}
				if (Creature.HealthComponent)
				{
					Creature.HealthComponent->ApplyBase3Part2(Packet);
				}
				break;
			case 4:
			{
				USWGMovementComponent* Movement = Creature.GetSWGMovementComponent();
				if (Movement)
				{
					Movement->ApplyBase4Part1(Packet);
				}
				if (Creature.EncumbranceComponent)
				{
					Creature.EncumbranceComponent->ApplyBase4(Packet);
				}
				if (Creature.SkillComponent)
				{
					Creature.SkillComponent->ApplyBase4(Packet);
				}
				if (Movement)
				{
					Movement->ApplyBase4Part2(Packet);
				}
				if (Creature.SpaceMissionComponent)
				{
					Creature.SpaceMissionComponent->ApplyBase4Part1(Packet);
				}
				if (Movement)
				{
					Movement->ApplyBase4Part3(Packet);
				}
				if (Creature.SpaceMissionComponent)
				{
					Creature.SpaceMissionComponent->ApplyBase4Part2(Packet);
				}
				break;
			}
			case 6:
				// TangibleObjectMessage6 fields (Unknown076 + DefenderList) come first.
				if (Creature.DefenderComponent)
				{
					Creature.DefenderComponent->ApplyBase6(Packet);
				}
				Creature.Level = Packet.ReadUInt16();
				if (Creature.PerformanceComponent)
				{
					Creature.PerformanceComponent->ApplyBase6Part1(Packet);
				}
				if (Creature.CombatStateComponent)
				{
					Creature.CombatStateComponent->ApplyBase6Part1(Packet);
				}
				if (Creature.GroupComponent)
				{
					Creature.GroupComponent->ApplyBase6(Packet);
				}
				Creature.GuildId = Packet.ReadInt32();
				if (Creature.CombatStateComponent)
				{
					Creature.CombatStateComponent->ApplyBase6Part2(Packet);
				}
				if (Creature.PerformanceComponent)
				{
					Creature.PerformanceComponent->ApplyBase6Part2(Packet);
				}
				if (Creature.HealthComponent)
				{
					Creature.HealthComponent->ApplyBase6(Packet);
				}
				if (Creature.EquipmentComponent)
				{
					Creature.EquipmentComponent->ApplyBase6(Packet);
				}
				if (Creature.CombatStateComponent)
				{
					Creature.CombatStateComponent->ApplyBase6Part3(Packet);
				}
				break;
			default:
				UE_LOG(LogTemp, Verbose, TEXT("USWGObjectGraphSubsystem: no CREO baseline dispatch for slot %d"), Slot);
				break;
		}
	}
}

void USWGObjectGraphSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Network = Cast<USWGNetworkSubsystem>(Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));
	MeshGenerator = Cast<USWGMeshGeneratorSubsystem>(Collection.InitializeDependency(USWGMeshGeneratorSubsystem::StaticClass()));
	TerrainSubsystem = Cast<USWGTerrainSubsystem>(Collection.InitializeDependency(USWGTerrainSubsystem::StaticClass()));

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
	// Unlike Position (confirmed 1:1, no swap — see FSWGZoneLoadingState::Enter's
	// comment), a direct X,Y,Z,W copy here produced wildly wrong pitch (~-89 deg
	// for standing NPCs, confirmed via a live actor's transform) — the rotation
	// wire data isn't in the same basis as position. Swapping Y/Z is the standard
	// Y-up-to-Z-up quaternion conversion; testing empirically against a live actor.
	const FQuat Rotation(Msg.DirX, Msg.DirZ, Msg.DirY, Msg.DirW);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* NewActor = World->SpawnActor<AActor>(ActorClass, FTransform(Rotation, Location), SpawnParams);
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

	if (ActorClass->IsChildOf(ASWGCreature::StaticClass()) || ActorClass->IsChildOf(ASWGPlayer::StaticClass()) || ActorClass->IsChildOf(ASWGItem::StaticClass()))
	{
		MeshGenerator->RequestMesh(NewActor, Msg.ObjectCrc);
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

	FSWGPacket Sub(Msg.RawPayload.GetData(), Msg.RawPayload.Num());
	const FString FourCC = Msg.GetObjectTypeFourCC();

	UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: Baselines object=%lld FourCC=%s slot=%d actor=%s"),
		Msg.ObjectId, *FourCC, Msg.BaselineType, *Actor->GetName());

	if (FourCC == TEXT("TANO") || FourCC == TEXT("WEAO"))
	{
		// WEAO (weapon objects, FORM tag SWOT) is a distinct wire FourCC from
		// plain TANO, but both resolve to ASWGItem (see FSWGFormTagMapping) and
		// share the same TangibleObjectMessage3/6-derived baseline layout.
		if (ASWGItem* Item = Cast<ASWGItem>(Actor))
			ApplyTangibleBaseline(*Item, Msg.BaselineType, Sub);
	}
	else if (FourCC == TEXT("CREO"))
	{
		if (ASWGCreature* Creature = Cast<ASWGCreature>(Actor))
			ApplyCreatureBaseline(*Creature, Msg.BaselineType, Sub);
	}
	else
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
	// ContainerId. If that's already arrived by now, stay hidden instead of
	// revealing it as a free-floating world actor at its (usually (0,0,0))
	// raw position — that position is container-relative, not world-relative.
	if (const int64* ContainerId = ContainerByObjectId.Find(Msg.ObjectId); ContainerId && *ContainerId != 0)
	{
		UE_LOG(LogTemp, Log, TEXT("USWGObjectGraphSubsystem: SceneEndBaselines object=%lld actor=%s — staying hidden (contained in %lld)"),
			Msg.ObjectId, *Actor->GetName(), *ContainerId);
		OnObjectReady.Broadcast(Msg.ObjectId);
		return;
	}

	Actor->SetActorHiddenInGame(false);
	Actor->SetActorEnableCollision(true);

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
	if (Actor)
	{
		ApplyContainment(Actor, Msg.ContainerId);
	}
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
	Actor->SetActorLocation(GroundedLocationFor(Actor, SWGToUnrealSpace(FVector(Msg.PosX, Msg.PosY, Msg.PosZ))));

	// DirectionAngle is a single byte (0-255) mapping to a full 0-360 degree
	// yaw — cheap per-tick facing without transmitting a full quaternion like
	// the initial spawn does. Pitch/Roll aren't part of this message (walking
	// creatures don't need them), so only Yaw changes here.
	const float YawDegrees = (Msg.DirectionAngle / 256.0f) * 360.0f;
	FRotator NewRotation = Actor->GetActorRotation();
	NewRotation.Yaw = YawDegrees;
	Actor->SetActorRotation(NewRotation);
}

void USWGObjectGraphSubsystem::ApplyContainment(AActor* Actor, int64 ContainerId)
{
	if (!Actor)
	{
		return;
	}

	const bool bContained = ContainerId != 0;
	Actor->SetActorHiddenInGame(bContained);
	Actor->SetActorEnableCollision(!bContained);
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
