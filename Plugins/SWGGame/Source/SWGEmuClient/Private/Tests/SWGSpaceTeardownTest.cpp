#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/Event.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SWGTangibleComponent.h"
#include "Objects/World/SWGShip.h"
#include "Objects/World/SWGCell.h"
#include "Objects/Player/SWGPlayer.h"
#include "Objects/Tangible/SWGItem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGSkeletalAnimationPipeline.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGMeshReader.h"
#include "TRE/SWGSkeletonReader.h"
#include "Network/SWGPacket.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/UpdateContainmentMessage.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceTeardownTest, "SWG.Space.Teardown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpaceTeardownTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine); Game->InitializeStandalone();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) { Game->Shutdown(); return false; }
	Game->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(Game); World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { Game->Shutdown(); World->DestroyWorld(false); };
	USWGObjectGraphSubsystem* Graph = Game->GetSubsystem<USWGObjectGraphSubsystem>();
	ASWGShip* Ship = World->SpawnActor<ASWGShip>(); ASWGCell* Room = World->SpawnActor<ASWGCell>();
	ASWGCell* DeferredRoom = World->SpawnActor<ASWGCell>(); ASWGItem* Seat = World->SpawnActor<ASWGItem>();
	ASWGPlayer* Player = World->SpawnActor<ASWGPlayer>(); ASWGItem* Gear = World->SpawnActor<ASWGItem>();
	ASWGItem* Prop = World->SpawnActor<ASWGItem>(); ASWGItem* PropChild = World->SpawnActor<ASWGItem>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!Graph || !Ship || !Room || !DeferredRoom || !Seat || !Player || !Gear || !Prop || !PropChild || !Controller) return false;
	Graph->bSpaceScene = true; Graph->LocalPlayerObjectId = 1003;
	Graph->RegisterStaticObject(1000,Ship); Graph->RegisterStaticObject(1001,Room,1000);
	Graph->RegisterStaticObject(1002,Seat,1001); Graph->RegisterStaticObject(1003,Player,1001);
	Graph->RegisterStaticObject(1005,Gear,1003); Graph->RegisterStaticObject(1006,DeferredRoom,1000);
	Graph->ContainerByObjectId.Add(1004,1003); Graph->ResolvePlayerObjectActor(1004);
	ASWGPlayer* Remote = World->SpawnActor<ASWGPlayer>();
	if (!Remote) return false;
	Graph->RegisterStaticObject(1007,Remote,1001); Graph->ContainerByObjectId.Add(1008,1007); Graph->ResolvePlayerObjectActor(1008);
	Graph->ReadyObjects.Add(1008); Graph->ObjectCrcById.Add(1008,987);
	Graph->ContainmentTypeByObjectId.Add(1003,int32(ESWGContainmentType::VolumeContained));
	Ship->ShipState.ShipId=42; Graph->ShipObjectIds.Add(42,1000); Graph->SpaceObjectPoses.Add(1000,FTransform::Identity);
	Graph->ObjectCrcById.Add(1000,123); Graph->CellNumberByObjectId.Add(1001,1);
	Room->OwningBuilding=Ship; Ship->Cells.Add(Room); Room->AttachToActor(Ship,FAttachmentTransformRules::KeepRelativeTransform);
	Seat->AttachToActor(Room,FAttachmentTransformRules::KeepRelativeTransform);
	Player->AttachToActor(Seat,FAttachmentTransformRules::KeepWorldTransform); Gear->AttachToActor(Player,FAttachmentTransformRules::KeepRelativeTransform);
	Room->InteriorActors.Add(Prop); PropChild->AttachToActor(Prop,FAttachmentTransformRules::KeepRelativeTransform);
	Graph->StoredShipInteriorVisibility.Add(Prop,{false,true});
	Player->SetActorLocation(FVector(300,400,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())); Player->LastNetworkZ=0;
	Controller->Possess(Player); Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	World->GetWorldSettings()->NotifyBeginPlay();
	const FTransform Before = Player->GetActorTransform();
	Graph->RemoveObject(1000);
	TestTrue(TEXT("ship and initialized room are destroyed"), Ship->IsActorBeingDestroyed() && Room->IsActorBeingDestroyed());
	TestTrue(TEXT("unfinished unattached network room is destroyed"), DeferredRoom->IsActorBeingDestroyed());
	TestTrue(TEXT("unattached layout prop and its child are destroyed"), Prop->IsActorBeingDestroyed() && PropChild->IsActorBeingDestroyed());
	TestTrue(TEXT("seat is destroyed without destroying the local body or gear"), Seat->IsActorBeingDestroyed() && !Player->IsActorBeingDestroyed() && !Gear->IsActorBeingDestroyed());
	TestTrue(TEXT("controller retains its pawn"), Controller->GetPawn() == Player);
	TestEqual(TEXT("local body remains registered"), Graph->FindActor(1003), static_cast<AActor*>(Player));
	TestEqual(TEXT("local PLAY alias remains registered"), Graph->FindActor(1004), static_cast<AActor*>(Player));
	TestEqual(TEXT("local gear remains registered"), Graph->FindActor(1005), static_cast<AActor*>(Gear));
	TestNull(TEXT("body detaches from removed seat"), Player->GetAttachParentActor());
	TestTrue(TEXT("detaching preserves the body transform"), Player->GetActorTransform().Equals(Before));
	World->Tick(LEVELTICK_All,.25f);
	TestTrue(TEXT("actual world tick cannot drop the preserved body through its removed floor"), Player->GetActorTransform().Equals(Before,.01));
	TestEqual(TEXT("missing floor cannot start a gravity fall"), Player->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	TestTrue(TEXT("body waits for valid replacement collision"), Player->bAwaitingCellPlacement);
	TestFalse(TEXT("removed frame cannot receive walking packets"), Graph->IsLocalSpaceSceneReady());
	TestFalse(TEXT("removed ship movement ID is cleared"), Graph->ShipObjectIds.Contains(42));
	TestFalse(TEXT("removed ship pose and CRC are cleared"), Graph->SpaceObjectPoses.Contains(1000) || Graph->ObjectCrcById.Contains(1000));
	TestFalse(TEXT("removed room state is cleared"), Graph->ActorRegistry.Contains(1001) || Graph->ContainerByObjectId.Contains(1001) || Graph->CellNumberByObjectId.Contains(1001) || Graph->ReadyObjects.Contains(1001));
	TestFalse(TEXT("removed layout visibility state is cleared"), Graph->StoredShipInteriorVisibility.Contains(Prop));
	TestTrue(TEXT("unattached remote occupant is removed with its room"), Remote->IsActorBeingDestroyed());
	TestFalse(TEXT("remote PLAY alias state is removed with its physical body"), Graph->ActorRegistry.Contains(1008) || Graph->ContainerByObjectId.Contains(1008)
		|| Graph->ReadyObjects.Contains(1008) || Graph->ObjectCrcById.Contains(1008));
	Graph->RemoveObject(1000); // late duplicate destroy must preserve the controller too
	TestTrue(TEXT("late duplicate destroy keeps the pawn"), Controller->GetPawn() == Player);
	ASWGShip* Replacement = World->SpawnActor<ASWGShip>(); ASWGCell* ReplacementRoom = World->SpawnActor<ASWGCell>();
	if (!Replacement || !ReplacementRoom) return false;
	Graph->RegisterStaticObject(2000,Replacement); Graph->RegisterStaticObject(2001,ReplacementRoom,2000);
	Replacement->bHasFlightBaseline=true; Replacement->TangibleComponent->bHasBase3=true;
	ReplacementRoom->OwningBuilding=Replacement; ReplacementRoom->bCollisionReady=true;
	ReplacementRoom->AttachToActor(Replacement,FAttachmentTransformRules::KeepRelativeTransform);
	UStaticMeshComponent* Hull = NewObject<UStaticMeshComponent>(Replacement); Hull->SetupAttachment(Replacement->GetRootComponent());
	Hull->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"))); Hull->RegisterComponent();
	FSWGPacket Packet; Packet.WriteUInt16(4); Packet.WriteUInt32(uint32(ESWGMessageOp::UpdateContainmentMessage));
	Packet.WriteUInt64(1003); Packet.WriteUInt64(2001); Packet.WriteUInt32(uint32(ESWGContainmentType::VolumeContained));
	FSWGMessage Reader(Packet); FUpdateContainmentMessage Update(uint32(ESWGMessageOp::UpdateContainmentMessage),Reader);
	Graph->HandleUpdateContainment(Update); Graph->Tick(.016f);
	TestTrue(TEXT("server-confirmed replacement room restores readiness"), Graph->IsLocalSpaceSceneReady());
	TestFalse(TEXT("ready replacement releases placement wait"), Player->bAwaitingCellPlacement);
	TestEqual(TEXT("ready replacement restores walking"), Player->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
	TestTrue(TEXT("ready replacement preserves feet and restores collision"), Player->GetActorTransform().Equals(Before,.01) && Player->GetActorEnableCollision());
	USWGMeshGeneratorSubsystem* Meshes = Game->GetSubsystem<USWGMeshGeneratorSubsystem>();
	if (!Meshes) return false;
	auto HullRequest = Meshes->RequestMesh(Replacement, TEXT("appearance/mesh/yt1300_ladder_l0.msh"));
	auto RoomRequest = Meshes->RequestMesh(ReplacementRoom, TEXT("appearance/mesh/yt1300_ladder_l0.msh"));
	TestFalse(TEXT("mesh requests are queued before a worker tick"), HullRequest.IsReady() || RoomRequest.IsReady());
	FSWGSkeletalAnimationPipeline* Pipeline = Meshes->SkeletalAnimationPipeline;
	if (!Pipeline) return false;
	FSWGSkeletalMeshBuildRequest PendingMesh;
	PendingMesh.Promise = MakeShared<TPromise<USkeletalMesh*>>();
	auto PendingMeshFuture = PendingMesh.Promise->GetFuture().Next([LoadingOwner = Meshes->TrackLoading(Player)](USkeletalMesh* Mesh) { return Mesh; });
	Pipeline->PendingSkeletalMeshRequests.Enqueue(MoveTemp(PendingMesh));
	TestTrue(TEXT("deferred skeletal continuation retains loading ownership"), Meshes->HasPendingMeshes([Player](const AActor* Actor) { return Actor == Player; }));
	FSWGSkeletalMeshBuildResult CompletedMesh;
	CompletedMesh.Promise = MakeShared<TPromise<USkeletalMesh*>>();
	auto CompletedMeshFuture = CompletedMesh.Promise->GetFuture();
	Pipeline->CompletedSkeletalMeshBuilds.Enqueue(MoveTemp(CompletedMesh));
	FSWGAnimSequenceBuildRequest PendingAnim;
	PendingAnim.Promise = MakeShared<TPromise<UAnimSequence*>>();
	auto PendingAnimFuture = PendingAnim.Promise->GetFuture();
	Pipeline->PendingAnimSequenceRequests.Enqueue(MoveTemp(PendingAnim));
	FSWGAnimSequenceBuildResult CompletedAnim;
	CompletedAnim.Promise = MakeShared<TPromise<UAnimSequence*>>();
	auto CompletedAnimFuture = CompletedAnim.Promise->GetFuture();
	Pipeline->CompletedAnimSequenceBuilds.Enqueue(MoveTemp(CompletedAnim));
	Meshes->Deinitialize();
	TestFalse(TEXT("skeletal cancellation completes its deferred loading ownership"), Meshes->HasPendingMeshes([Player](const AActor* Actor) { return Actor == Player; }));
	if (!TestTrue(TEXT("shutdown fulfills every queued mesh promise"), HullRequest.IsReady() && RoomRequest.IsReady())) return false;
	TestTrue(TEXT("cancelled mesh requests return empty results"), HullRequest.Get().MeshOrComponent.IsType<FEmptyVariantState>()
		&& RoomRequest.Get().MeshOrComponent.IsType<FEmptyVariantState>());
	if (!TestTrue(TEXT("shutdown resolves pending and completed skeletal/animation queues"), PendingMeshFuture.IsReady()
		&& CompletedMeshFuture.IsReady() && PendingAnimFuture.IsReady() && CompletedAnimFuture.IsReady())) return false;
	TestTrue(TEXT("retired skeletal and animation requests return cancellation"), !PendingMeshFuture.Get()
		&& !CompletedMeshFuture.Get() && !PendingAnimFuture.Get() && !CompletedAnimFuture.Get());
	auto AfterShutdown = Meshes->RequestMesh(Replacement, TEXT("appearance/mesh/yt1300_ladder_l0.msh"));
	if (TestTrue(TEXT("new request after shutdown resolves immediately"), AfterShutdown.IsReady()))
		TestTrue(TEXT("new request after shutdown is cancelled"), AfterShutdown.Get().MeshOrComponent.IsType<FEmptyVariantState>());
	TestEqual(TEXT("no parsing tasks survive shutdown"), Meshes->ParsingTasks.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceSkeletalLifetimeTest, "SWG.Space.SkeletalWorkerLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

bool FSWGSpaceSkeletalLifetimeTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->AddToRoot(); Game->InitializeStandalone();
	UWorld* World = Game->GetWorldContext()->World();
	if (!World) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
	USWGMeshGeneratorSubsystem* Meshes = Game->GetSubsystem<USWGMeshGeneratorSubsystem>();
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	FSWGSkeletalAnimationPipeline* Pipeline = Meshes ? Meshes->SkeletalAnimationPipeline : nullptr;
	FEvent* EnteredBuild = FPlatformProcess::GetSynchEventFromPool(true);
	std::atomic<bool> Built{false};
	std::atomic<bool> ShutdownObserved{false};
	ON_SCOPE_EXIT
	{
		Game->Shutdown();
		if (Pipeline) { Pipeline->BeforeSkeletalBuildForTest = {}; Pipeline->AfterSkeletalBuildForTest = {}; }
		FPlatformProcess::ReturnSynchEventToPool(EnteredBuild);
		GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
		World->RemoveFromRoot(); Game->RemoveFromRoot();
	};
	if (!Pipeline || !Tre) return false;
	Tre->LoadConfig();
	if (!TestTrue(TEXT("skeletal lifetime fixture loads real archives"), Tre->LoadArchives())) return false;
	FSWGSkeletalMeshBuildRequest Request;
	TMap<FString, FString> AnimationPaths;
	FString AppearancePath;
	bool Skeletal = false;
	if (!TestTrue(TEXT("actual human body appearance resolves"), Meshes->ResolveMeshPathForTemplate(
		TEXT("object/creature/player/shared_human_male.iff"), Request.MeshVirtualPaths, AnimationPaths, Skeletal, AppearancePath))) return false;
	if (!TestTrue(TEXT("human appearance has skeletal parts"), Skeletal && !Request.MeshVirtualPaths.IsEmpty())) return false;
	Request.SkeletonPath = FSWGMeshReader::ReadSkeletalMeshSkeletonPath(Tre->CreateIffReader(Request.MeshVirtualPaths[0]));
	if (!TestTrue(TEXT("actual human skeleton loads"), FSWGSkeletonReader::ReadSkeleton(Tre->CreateIffReader(Request.SkeletonPath), Request.Skeleton))) return false;
	Request.SlotHardpoints = Pipeline->GetSlotHardpoints();
	Request.PackagePath = TEXT("/Game/SWGEmu/Generated/SK_LifetimeFixture");
	Request.Promise = MakeShared<TPromise<USkeletalMesh*>>();
	auto Future = Request.Promise->GetFuture();
	Pipeline->BeforeSkeletalBuildForTest = [Pipeline, EnteredBuild, &ShutdownObserved]
	{
		EnteredBuild->Trigger();
		const double Started = FPlatformTime::Seconds();
		while (!Pipeline->bShuttingDown.load() && FPlatformTime::Seconds()-Started < 10)
			FPlatformProcess::Sleep(.001f);
		ShutdownObserved = Pipeline->bShuttingDown.load();
	};
	Pipeline->AfterSkeletalBuildForTest = [&Built](bool Success) { Built = Success; };
	// Bypass the saved-asset cache without deleting assets; run the actual pump/importer.
	Pipeline->PendingSkeletalMeshRequests.Enqueue(MoveTemp(Request));
	Pipeline->Tick(0);
	if (!TestTrue(TEXT("actual worker reaches the importer before shutdown"), EnteredBuild->Wait(5000))) return false;
	TestTrue(TEXT("skeletal worker is active at shutdown"), Pipeline->NumActiveWorkerTasks.load() > 0);
	Tre->Deinitialize();
	TestTrue(TEXT("import runs while shutdown waits for its worker"), ShutdownObserved.load());
	TestTrue(TEXT("actual humanoid skeletal import succeeds during shutdown"), Built.load());
	TestEqual(TEXT("no skeletal worker survives archive clearing"), Pipeline->NumActiveWorkerTasks.load(), 0);
	if (TestTrue(TEXT("shutdown resolves actual import promise"), Future.IsReady()))
		TestNull(TEXT("retired import never finalizes a skeletal asset"), Future.Get());
	TestTrue(TEXT("completed import queue is drained"), Pipeline->CompletedSkeletalMeshBuilds.IsEmpty());
	TestFalse(TEXT("archives clear after the skeletal worker drains"), Tre->IsLoaded());
	return true;
}
#endif
