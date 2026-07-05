

#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGFormTagMapping.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/SceneCreateObjectMessage.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/Zone/SceneEndBaselinesMessage.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/Zone/CmdStartSceneMessage.h"

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
	// Dispatch order below follows each slot's Core3 field-declaration order
	// (see world-object-plan.html "Component breakdown" / "Delta application"),
	// but every component's ApplyBaseX is still a TODO no-op today — none of
	// them actually consume packet bytes yet. Once real field reads are added,
	// several slots interleave components mid-stream (e.g. CREO base3 is
	// CombatState, then loose actor fields, then Health, then CombatState
	// again, then Health again) — calling each component once per slot, as
	// done here, will no longer be byte-correct at that point and these
	// functions will need splitting into per-field-range calls instead.

	void ApplyTangibleBaseline(ASWGItem& Item, uint8 Slot, FSWGPacket& Packet)
	{
		switch (Slot)
		{
			case 3:
				if (Item.TangibleComponent) 
				{
					Item.TangibleComponent->ApplyBase3(Packet);
				}
				if (Item.ConditionComponent)
				{
					Item.ConditionComponent->ApplyBase3(Packet);
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

	void ApplyCreatureBaseline(ASWGCreature& Creature, uint8 Slot, FSWGPacket& Packet)
	{
		switch (Slot)
		{
			case 1:
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
				if (Creature.TangibleComponent) 
				{
					Creature.TangibleComponent->ApplyBase3(Packet);
				}
				if (Creature.ConditionComponent) 
				{
					Creature.ConditionComponent->ApplyBase3(Packet);
				}
				if (Creature.CombatStateComponent)
				{
					Creature.CombatStateComponent->ApplyBase3(Packet);
				}
				if (Creature.HealthComponent) 
				{
					Creature.HealthComponent->ApplyBase3(Packet);
				}
				break;
			case 4:
				if (USWGMovementComponent* Movement = Creature.GetSWGMovementComponent())
				{
					Movement->ApplyBase4(Packet);
				}
				if (Creature.EncumbranceComponent) 
				{
					Creature.EncumbranceComponent->ApplyBase4(Packet); 
				}
				if (Creature.SkillComponent) 
				{
					Creature.SkillComponent->ApplyBase4(Packet);
				}
				if (Creature.SpaceMissionComponent) 
				{
					Creature.SpaceMissionComponent->ApplyBase4(Packet);
				}
				break;
			case 6:
				if (Creature.DefenderComponent) 
				{
					Creature.DefenderComponent->ApplyBase6(Packet);
				}
				if (Creature.PerformanceComponent)
				{
					Creature.PerformanceComponent->ApplyBase6(Packet);
				}
				if (Creature.CombatStateComponent)
				{
					Creature.CombatStateComponent->ApplyBase6(Packet);
				}
				if (Creature.GroupComponent) 
				{
					Creature.GroupComponent->ApplyBase6(Packet);
				}
				if (Creature.HealthComponent)
				{
					Creature.HealthComponent->ApplyBase6(Packet);
				}
				if (Creature.EquipmentComponent) 
				{
					Creature.EquipmentComponent->ApplyBase6(Packet);
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

AActor* USWGObjectGraphSubsystem::FindActor(int64 ObjectId) const
{
	if (const TWeakObjectPtr<AActor>* Found = ActorRegistry.Find(ObjectId))
		return Found->Get();
	return nullptr;
}

void USWGObjectGraphSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg)
{
	if (!Msg.IsValid())
		return;

	const uint32 Opcode = Msg->Opcode;

	if (Opcode == static_cast<uint32>(ESWGMessageOp::CmdStartScene))
	{
		HandleCmdStartScene(*static_cast<const FCmdStartSceneMessage*>(Msg.Get()));
	}
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::SceneCreateObjectByCrc))
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

	const FVector Location(Msg.PosX, Msg.PosY, Msg.PosZ);
	const FQuat Rotation(Msg.DirX, Msg.DirY, Msg.DirZ, Msg.DirW);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	//SpawnParams.OverrideLevel = GetSpawnLevel(); // null falls back to default (PersistentLevel) placement

	AActor* NewActor = World->SpawnActor<AActor>(ActorClass, FTransform(Rotation, Location), SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGObjectGraphSubsystem: failed to spawn %s for object %lld"), *ActorClass->GetName(), Msg.ObjectId);
		return;
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

	Actor->SetActorHiddenInGame(false);
	Actor->SetActorEnableCollision(true);

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
