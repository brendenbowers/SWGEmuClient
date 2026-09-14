#include "Subsystems/SWGTargetSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Components/SWGCombatStateComponent.h"
#include "Components/SWGTangibleComponent.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "GameFramework/Character.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Network/Messages/Zone/Object/TargetUpdate.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

void USWGTargetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Network = Cast<USWGNetworkSubsystem>(Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));
	ObjectGraph = Cast<USWGObjectGraphSubsystem>(Collection.InitializeDependency(USWGObjectGraphSubsystem::StaticClass()));

	if (ObjectGraph)
	{
		ObjectDestroyedHandle = ObjectGraph->OnObjectDestroyed.AddUObject(this, &USWGTargetSubsystem::HandleObjectDestroyed);
	}

	MeshGenerator = Cast<USWGMeshGeneratorSubsystem>(Collection.InitializeDependency(USWGMeshGeneratorSubsystem::StaticClass()));
	if (MeshGenerator)
	{
		MeshReadyHandle = MeshGenerator->OnMeshReady.AddUObject(this, &USWGTargetSubsystem::HandleMeshReady);
	}
}

void USWGTargetSubsystem::Deinitialize()
{
	if (ObjectGraph && ObjectDestroyedHandle.IsValid())
	{
		ObjectGraph->OnObjectDestroyed.Remove(ObjectDestroyedHandle);
		ObjectDestroyedHandle.Reset();
	}

	// Drop the outline before the graph tears its actors down, so nothing is
	// left rendering into custom depth if this session is replaced by another.
	SetHighlightEnabled(CurrentTargetId, false);

	if (MeshGenerator && MeshReadyHandle.IsValid())
	{
		MeshGenerator->OnMeshReady.Remove(MeshReadyHandle);
		MeshReadyHandle.Reset();
	}

	CurrentTargetId = 0;
	LastServerTargetId = 0;
	bSeenServerTarget = false;
	SeenForPlayerObjectId = 0;
	Network = nullptr;
	ObjectGraph = nullptr;
	MeshGenerator = nullptr;
}

bool USWGTargetSubsystem::IsSelectable(const AActor* Actor)
{
	return Actor && Actor->FindComponentByClass<USWGTangibleComponent>() != nullptr;
}

void USWGTargetSubsystem::MakeSelectable(UPrimitiveComponent* Component)
{
	if (!Component)
	{
		return;
	}

	// Query-only, and blocking on nothing but the selection channel. A mesh
	// that was previously collision-free must stay collision-free for
	// movement and physics — enabling it wholesale here would drop creatures
	// out of the world or wall the player in.
	Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Component->SetCollisionResponseToAllChannels(ECR_Ignore);
	Component->SetCollisionResponseToChannel(SelectionChannel, ECR_Block);
}

TStatId USWGTargetSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGTargetSubsystem, STATGROUP_Tickables);
}

bool USWGTargetSubsystem::IsTickable() const
{
	return ObjectGraph != nullptr;
}

void USWGTargetSubsystem::Tick(float DeltaTime)
{
	TimeSinceReconcile += DeltaTime;
	if (TimeSinceReconcile < ReconcileInterval)
	{
		return;
	}
	TimeSinceReconcile = 0.f;

	ReconcileWithServerTarget();
}

void USWGTargetSubsystem::ReconcileWithServerTarget()
{
	if (!ObjectGraph)
	{
		return;
	}

	const int64 PlayerObjectId = ObjectGraph->GetLocalPlayerObjectId();
	if (PlayerObjectId == 0)
	{
		return;
	}

	// A new player object means a new session: re-seed rather than compare
	// against the previous character's target.
	if (PlayerObjectId != SeenForPlayerObjectId)
	{
		SeenForPlayerObjectId = PlayerObjectId;
		bSeenServerTarget = false;
	}

	const USWGCombatStateComponent* CombatState =
		ObjectGraph->FindComponent<USWGCombatStateComponent>(PlayerObjectId);

	// Nothing to compare against until our own CREO base6 has landed. The
	// field reads 0 before then, which is indistinguishable from "no target"
	// and would wipe anything clicked in the meantime.
	if (!CombatState || !CombatState->bHasBase6)
	{
		return;
	}

	const int64 ServerTargetId = CombatState->TargetId;

	if (!bSeenServerTarget)
	{
		bSeenServerTarget = true;
		LastServerTargetId = ServerTargetId;

		// Seed only from a real target, so reconnecting mid-session can
		// restore one the server remembers without a baseline's empty field
		// clearing a selection the player has already made.
		if (ServerTargetId != 0)
		{
			ApplyTarget(ServerTargetId);
		}
		return;
	}

	// Edge-triggered on purpose. setTargetID broadcasts with sendSelf false,
	// so this field does not move in response to our own SetTarget — only a
	// genuine server-side change should override the player's click.
	if (ServerTargetId != LastServerTargetId)
	{
		LastServerTargetId = ServerTargetId;
		ApplyTarget(ServerTargetId);
	}
}

void USWGTargetSubsystem::SetTarget(int64 ObjectId)
{
	if (ObjectId == CurrentTargetId)
	{
		return;
	}

	const int64 PlayerObjectId = ObjectGraph ? ObjectGraph->GetLocalPlayerObjectId() : 0;
	if (Network && PlayerObjectId != 0)
	{
		FTargetUpdate Update(static_cast<uint64>(PlayerObjectId), static_cast<uint64>(ObjectId));
		Network->SendMessage(Update.Serialize());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTargetSubsystem: targeting %lld locally only — no network or no local player yet"), ObjectId);
	}

	ApplyTarget(ObjectId);
}

void USWGTargetSubsystem::SetTargetActor(AActor* Actor)
{
	const ISWGNetworkObjectInterface* NetworkObject = Cast<ISWGNetworkObjectInterface>(Actor);
	SetTarget(NetworkObject ? NetworkObject->GetObjectId() : 0);
}

AActor* USWGTargetSubsystem::GetTargetActor() const
{
	return (ObjectGraph && CurrentTargetId != 0) ? ObjectGraph->FindActor(CurrentTargetId) : nullptr;
}

void USWGTargetSubsystem::ApplyTarget(int64 NewTargetId)
{
	if (NewTargetId == CurrentTargetId)
	{
		return;
	}

	SetHighlightEnabled(CurrentTargetId, false);
	CurrentTargetId = NewTargetId;
	SetHighlightEnabled(CurrentTargetId, true);

	OnTargetChanged.Broadcast(CurrentTargetId, GetTargetActor());
}

void USWGTargetSubsystem::SetHighlightEnabled(int64 ObjectId, bool bEnabled)
{
	AActor* Actor = (ObjectGraph && ObjectId != 0) ? ObjectGraph->FindActor(ObjectId) : nullptr;
	if (!Actor)
	{
		return;
	}

	// Every primitive, not just the root mesh: creature meshes are built as
	// several components (body plus worn items), and outlining only one of
	// them draws a partial silhouette.
	Actor->ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors*/ true,
		[bEnabled](UPrimitiveComponent* Primitive)
		{
			Primitive->SetRenderCustomDepth(bEnabled);
			Primitive->SetCustomDepthStencilValue(bEnabled ? TargetStencilValue : 0);
		});
}

void USWGTargetSubsystem::HandleMeshReady(AActor* Actor, UMeshComponent* MeshComponent)
{
	if (!MeshComponent || !IsSelectable(Actor))
	{
		return;
	}

	// Creatures are picked via their capsule instead (see ASWGCreature's
	// constructor). Their generated static mesh gets hidden the moment the
	// skeletal pipeline swaps a real animated mesh in
	// (FSWGSkeletalAnimationPipeline), so making it clickable would leave a
	// stale invisible silhouette catching clicks alongside the live one.
	if (Actor->IsA<ACharacter>())
	{
		return;
	}

	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent);
	UStaticMesh* StaticMesh = StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr;

	if (UBodySetup* BodySetup = StaticMesh ? StaticMesh->GetBodySetup() : nullptr)
	{
		// These meshes are generated at runtime and carry no collision
		// geometry of their own, so a trace would pass straight through.
		// Complex-as-simple picks the render triangles up as query geometry —
		// pixel-accurate, and free of any authored collision primitive.
		//
		// The mesh assets are cached and shared between every actor using
		// them, so this runs once per asset rather than once per spawn.
		if (BodySetup->CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple)
		{
			BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
			BodySetup->InvalidatePhysicsData();
			BodySetup->CreatePhysicsMeshes();
		}
	}

	MakeSelectable(StaticMeshComponent);
}

void USWGTargetSubsystem::HandleObjectDestroyed(int64 ObjectId)
{
	if (ObjectId != CurrentTargetId)
	{
		return;
	}

	// The actor is still resolvable at this point but about to go away, so
	// clear the highlight through the normal path and then drop the id
	// without telling the server: it already knows the object is gone and
	// clears our target itself (CreatureObject::setTargetID on destroy).
	ApplyTarget(0);
}
