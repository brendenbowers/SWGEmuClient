#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Objects/World/SWGShip.h"
#include "Objects/World/SWGCell.h"
#include "Objects/Tangible/SWGItem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGShipLaunchSubsystem.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Network/Messages/Zone/ShipUpdateTransformMessage.h"
#include "Common/SWGWorldScale.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Objects/Creature/SWGCreature.h"
#include "Objects/Player/SWGPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Camera/CameraTypes.h"
#include "Camera/CameraComponent.h"
#include "Components/SWGMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/Zone/SceneEndBaselinesMessage.h"
#include "Network/Messages/Zone/SceneCreateObjectMessage.h"
#include "Network/Messages/Zone/UpdateContainmentMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/ObjControllerMessageIn.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceFrameTest, "SWG.Space.InteriorFrame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpaceFrameTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->InitializeStandalone();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) { Game->Shutdown(); return false; }
	Game->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(Game);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { Game->Shutdown(); World->DestroyWorld(false); };
	USWGObjectGraphSubsystem* Graph = NewObject<USWGObjectGraphSubsystem>(Game);
	ASWGShip* Ship = World->SpawnActor<ASWGShip>();
	ASWGShip* Other = World->SpawnActor<ASWGShip>();
	ASWGCell* Cell = World->SpawnActor<ASWGCell>();
	if (!Ship || !Other || !Cell) return false;
	Cell->AttachToActor(Ship, FAttachmentTransformRules::KeepRelativeTransform);
	Graph->RegisterStaticObject(100, Ship);
	Graph->RegisterStaticObject(101, Cell, 100);
	Graph->ContainerByObjectId.Add(102, 101); // pilot chair
	Graph->ContainerByObjectId.Add(103, 102); // seated player
	Graph->LocalPlayerObjectId = 103;
	Graph->bSpaceScene = true;
	Graph->RegisterStaticObject(200, Other);
	Graph->SpaceObjectPoses.Add(100, FTransform::Identity);
	Graph->SpaceObjectPoses.Add(200, FTransform::Identity);
	Ship->ShipState.ShipId = 42;
	Graph->ShipObjectIds.Add(42, 100);
	Other->SpacePose = FTransform(FQuat::Identity, FVector(10000, 0, 0));
	TestTrue(TEXT("chair/cell ancestry resolves ship"), Graph->GetLocalPlayerShip() == Ship);
	ASWGItem* Seat = World->SpawnActor<ASWGItem>();
	if (!Seat) return false;
	Seat->AttachToActor(Cell, FAttachmentTransformRules::KeepRelativeTransform);
	Graph->RegisterStaticObject(102, Seat, 101);
	Graph->ReadyObjects.Add(103);
	Graph->ContainmentTypeByObjectId.Add(103, 15);
	Ship->TangibleComponent->bHasBase3 = true;
	Cell->OwningBuilding = Ship; Cell->bCollisionReady = true;
	UStaticMeshComponent* Hull = NewObject<UStaticMeshComponent>(Ship);
	Hull->SetupAttachment(Ship->GetRootComponent());
	Hull->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Hull->RegisterComponent();
	TestFalse(TEXT("space readiness waits for flight baseline"), Graph->IsLocalSpaceSceneReady());
	Ship->bHasFlightBaseline = true;
	TestTrue(TEXT("ready ship, chair and room permit space reveal"), Graph->IsLocalSpaceSceneReady());
	Ship->SetActorHiddenInGame(true);
	TestTrue(TEXT("loading-screen actor hiding does not deadlock readiness"), Graph->IsLocalSpaceSceneReady());
	Ship->SetActorHiddenInGame(false);
	Hull->SetVisibility(false);
	TestFalse(TEXT("collision-only mesh cannot satisfy visible hull readiness"), Graph->IsLocalSpaceSceneReady());
	Hull->SetVisibility(true);
	Cell->bCollisionReady = false;
	TestFalse(TEXT("space readiness waits for actual room collision"), Graph->IsLocalSpaceSceneReady());
	Cell->bCollisionReady = true;
	Seat->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	TestFalse(TEXT("space readiness waits for seat attachment"), Graph->IsLocalSpaceSceneReady());
	Seat->AttachToActor(Cell, FAttachmentTransformRules::KeepRelativeTransform);
	ASWGCreature* Player = World->SpawnActor<ASWGCreature>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!Player || !Controller) return false;
	Graph->RegisterStaticObject(103, Player, 102);
	Player->bAwaitingCellPlacement = true; Cell->bCollisionReady = false;
	FSWGPacket EndBytes; EndBytes.WriteUInt16(2); EndBytes.WriteUInt32(0x2C436037); EndBytes.WriteUInt64(103);
	FSWGMessage EndReader(FSWGPacket(EndBytes.Data.GetData(), EndBytes.Data.Num()));
	FSceneEndBaselinesMessage End(0x2C436037, EndReader);
	Graph->HandleSceneEndBaselines(End);
	TestTrue(TEXT("scene end actually possesses the local pilot"), Controller->GetPawn() == Player);
	TestEqual(TEXT("possession does not restart pilot walking during loading"), Player->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	TestFalse(TEXT("loading pilot cannot collide with unfinished room"), Player->GetActorEnableCollision());
	Cell->bCollisionReady = true;
	UPointLightComponent* RoomLight = NewObject<UPointLightComponent>(Cell);
	RoomLight->SetupAttachment(Cell->GetRootComponent()); RoomLight->RegisterComponent();
	Cell->AddRoomLight(RoomLight); RoomLight->SetVisibility(false);
	Graph->TryPlaceSpacePilot();
	TestTrue(TEXT("seated pilot activates its containing room lights"), RoomLight->GetVisibleFlag());
	TestFalse(TEXT("placed pilot stays outside walking collision"), Player->GetActorEnableCollision());
	FShipUpdateTransformMessage Move;
	Move.bValid = true; Move.ShipId = 42; Move.SyncStamp = 100;
	Move.Position = FVector(10, 20, 30);
	Move.Rotation = FQuat(FVector::UpVector, UE_PI/2);
	Graph->HandleShipUpdateTransform(Move);
	TestTrue(TEXT("walking ship actor remains exactly fixed"), Ship->GetActorTransform().Equals(FTransform::Identity));
	TestTrue(TEXT("attached floor frame remains fixed"), Cell->GetActorTransform().Equals(FTransform::Identity));
	const FVector Expected = Ship->SpacePose.InverseTransformPosition(Other->SpacePose.GetLocation());
	TestTrue(TEXT("other ship viewed in local ship frame"), Other->GetActorLocation().Equals(Expected, .01));
	Move.Position = FVector(20, 20, 30);
	Graph->HandleShipUpdateTransform(Move);
	TestTrue(TEXT("equal recipient stamp still accepts new ship pose"), Ship->SpacePose.GetLocation().Equals(SWGToUnrealSpace(Move.Position), .01));
	FTransform Accepted = Ship->SpacePose;
	Move.Position = FVector(100, 100, 100); Move.SyncStamp = 99;
	Graph->HandleShipUpdateTransform(Move);
	TestTrue(TEXT("older ship movement ignored"), Ship->SpacePose.Equals(Accepted));
	// Retail's extended millisecond clock is truncated to32 wire bits; see RetailFlightMathEvidence.txt.
	Ship->LastSpaceStamp = 0xfffffff0;
	Move.SyncStamp = 0x10; Move.Position = FVector(21,22,23);
	Graph->HandleShipUpdateTransform(Move);
	TestTrue(TEXT("ship observer accepts timestamp across32-bit wrap"), Ship->SpacePose.GetLocation().Equals(SWGToUnrealSpace(Move.Position), .01));
	Accepted = Ship->SpacePose;
	Move.SyncStamp = 0xfffffff8; Move.Position = FVector(90,90,90);
	Graph->HandleShipUpdateTransform(Move);
	TestTrue(TEXT("late pre-wrap ship report rejected"), Ship->SpacePose.Equals(Accepted));
	Move.SyncStamp = 0x10; Move.Position = FVector(24,25,26);
	Graph->HandleShipUpdateTransform(Move);
	TestTrue(TEXT("equal post-wrap recipient stamp accepts changed ship pose"), Ship->SpacePose.GetLocation().Equals(SWGToUnrealSpace(Move.Position), .01));
	Accepted = Ship->SpacePose;
	Ship->bPredictingLocalFlight = true;
	Move.SyncStamp = 101;
	Graph->HandleShipUpdateTransform(Move);
	TestTrue(TEXT("pilot prediction ignores Core3 observer extrapolation"), Ship->SpacePose.Equals(Accepted));
	FSWGPacket Correction;
	Correction.WriteUInt16(5); Correction.WriteUInt32(0x80CE5E46);
	Correction.WriteUInt32(0x1B); Correction.WriteUInt32(0x71); Correction.WriteUInt64(100); Correction.WriteUInt32(0);
	Correction.WriteUInt32(25);
	const FQuat NativeCorrection = FQuat(FVector(1,2,3).GetSafeNormal(), .7);
	for (double Value : {NativeCorrection.X, NativeCorrection.Y, NativeCorrection.Z, NativeCorrection.W}) Correction.WriteFloat(Value);
	Correction.WriteFloat(70); Correction.WriteFloat(90); Correction.WriteFloat(80); Correction.WriteFloat(0);
	FSWGMessage CorrectionReader(FSWGPacket(Correction.Data.GetData(), Correction.Data.Num()));
	FObjControllerMessageIn Corrected(0x80CE5E46, CorrectionReader);
	Graph->HandleObjControllerMessage(Corrected);
	TestTrue(TEXT("authoritative correction applies full pitch roll yaw"), Ship->SpacePose.GetRotation().Equals(SWGNativeToUnrealRotation(NativeCorrection.X,NativeCorrection.Y,NativeCorrection.Z,NativeCorrection.W), .001));
	TestTrue(TEXT("authoritative correction preserves wire X Z Y"), Ship->SpacePose.GetLocation().Equals(SWGToUnrealSpace(FVector(70,80,90)), .001));
	TestTrue(TEXT("correction leaves walking room fixed"), Cell->GetActorTransform().Equals(FTransform::Identity));
	const FTransform CorrectedPose = Ship->SpacePose;
	Corrected.RawPayload.Pop();
	Graph->HandleObjControllerMessage(Corrected);
	TestTrue(TEXT("truncated correction cannot move the ship"), Ship->SpacePose.Equals(CorrectedPose));
	Ship->bPredictingLocalFlight = false;
	Other->ShipState.ShipId = 43;
	Graph->ShipObjectIds.Add(43, 200);
	Move.ShipId = 43; Move.SyncStamp = 10; Move.Position = FVector::ZeroVector; Move.Rotation = FQuat::Identity;
	Graph->HandleShipUpdateTransform(Move);
	Move.SyncStamp = 11; Move.Position = FVector(100,50,25); Move.Rotation = NativeCorrection;
	Graph->HandleShipUpdateTransform(Move);
	TestTrue(TEXT("remote update starts interpolation rather than jumping"), Other->SpacePose.GetLocation().IsNearlyZero());
	Graph->Tick(1.f/60.f);
	TestTrue(TEXT("remote ship interpolates height as well as horizontal axes"), Other->SpacePose.GetLocation().Z > 0 && Other->SpacePose.GetLocation().Z < 2500);
	TestTrue(TEXT("remote ship interpolates full quaternion"), !Other->SpacePose.GetRotation().Equals(FQuat::Identity, .001));
	Graph->Tick(1.f);
	TestTrue(TEXT("remote ship converges to transmitted pose"), Other->SpacePose.Equals(Other->ReplicatedSpacePose, .1));
	TestTrue(TEXT("remote smoothing leaves local floors fixed"), Cell->GetActorTransform().Equals(FTransform::Identity));
	ASWGCell* RemoteCell = World->SpawnActor<ASWGCell>();
	ASWGCreature* Passenger = World->SpawnActor<ASWGCreature>();
	if (!RemoteCell || !Passenger) return false;
	RemoteCell->OwningBuilding = Other;
	RemoteCell->AttachToActor(Other, FAttachmentTransformRules::KeepRelativeTransform);
	Graph->RegisterStaticObject(201, RemoteCell, 200);
	Graph->RegisterStaticObject(202, Passenger, 201);
	Passenger->PlacedInCell = RemoteCell;
	const FVector PassengerLocal(50,20,88);
	Passenger->SetActorTransform(FTransform(Other->GetActorQuat(), Other->GetActorTransform().TransformPosition(PassengerLocal)));
	Graph->ApplyNetworkTransform(Passenger, 202, Passenger->GetActorLocation(), Other->GetActorRotation().Yaw);
	USWGMovementComponent* PassengerMovement = Passenger->GetSWGMovementComponent();
	TestTrue(TEXT("object graph routes remote POB passengers to ship-local smoothing"), PassengerMovement->NetworkTargetLocation.IsSet() && PassengerMovement->NetworkTargetLocation->Equals(PassengerLocal, .01));
	Move.SyncStamp = 12; Move.Position += FVector(20,30,40);
	Graph->HandleShipUpdateTransform(Move); Graph->Tick(.1f);
	const FVector PassengerBeforeReport = Passenger->GetActorLocation();
	Graph->ApplyNetworkTransform(Passenger, 202, Other->GetActorTransform().TransformPosition(PassengerLocal), Other->GetActorRotation().Yaw);
	TestTrue(TEXT("fast ship motion does not force a passenger teleport"), Passenger->GetActorLocation().Equals(PassengerBeforeReport, .01));
	PassengerMovement->TickComponent(.1f, LEVELTICK_All, &PassengerMovement->PrimaryComponentTick);
	TestTrue(TEXT("passenger stays in its remote room during ship replication"), Passenger->GetActorTransform().GetRelativeTransform(Other->GetActorTransform()).GetLocation().Equals(PassengerLocal, .01));
	Graph->ContainerByObjectId.Add(103, 101); // walking, same stationary frame
	Graph->ContainmentTypeByObjectId.Add(103, int32(ESWGContainmentType::VolumeContained));
	TestTrue(TEXT("unseated cell ancestry retains ship"), Graph->GetLocalPlayerShip() == Ship);
	const FTransform PassengerShipBefore = Ship->SpacePose;
	Move.ShipId = 42; Move.SyncStamp = Ship->LastSpaceStamp + 1;
	Move.Position = FVector(80,80,90); Move.Rotation = FQuat::Identity;
	Graph->HandleShipUpdateTransform(Move);
	TestTrue(TEXT("a passenger's own ship uses observer smoothing"), Ship->bSmoothingSpacePose);
	TestTrue(TEXT("passenger window frame does not jump to the incoming target"), Ship->SpacePose.Equals(PassengerShipBefore));
	Graph->Tick(1.f/60.f);
	const double RemainingDistance = FVector::DistSquared(Ship->SpacePose.GetLocation(), Ship->ReplicatedSpacePose.GetLocation());
	TestTrue(TEXT("passenger window frame interpolates translation"), RemainingDistance > 0
		&& RemainingDistance < FVector::DistSquared(PassengerShipBefore.GetLocation(), Ship->ReplicatedSpacePose.GetLocation()));
	TestFalse(TEXT("passenger window frame interpolates rotation"), Ship->SpacePose.GetRotation().Equals(PassengerShipBefore.GetRotation(), .001));
	TestTrue(TEXT("passenger interpolation keeps the interior stationary"), Ship->GetActorTransform().Equals(FTransform::Identity)
		&& Cell->GetActorTransform().Equals(FTransform::Identity));
	Graph->Tick(1.f);
	TestTrue(TEXT("passenger frame converges to the pilot's reported pose"), Ship->SpacePose.Equals(Ship->ReplicatedSpacePose, .1));
	ASWGItem* Item = World->SpawnActor<ASWGItem>();
	if (!Item) return false;
	const FTransform LocalItem(FRotator(0, 15, 0), FVector(120, 30, 40));
	Item->bAwaitingCellPlacement = true;
	Graph->RegisterStaticObject(300, Item);
	Graph->SpaceObjectPoses.Add(300, LocalItem);
	Graph->RefreshSpaceFrame();
	Graph->ContainerByObjectId.Add(300, 101);
	Graph->ApplyContainment(Item, 300, 101);
	TestTrue(TEXT("late containment restores authored cell-local pose"), Item->GetActorTransform().Equals(LocalItem, .01));
	ASWGShip* StoredShip = World->SpawnActor<ASWGShip>();
	ASWGCell* StoredRoom = World->SpawnActor<ASWGCell>();
	if (!StoredShip || !StoredRoom) return false;
	Graph->RegisterStaticObject(600, StoredShip, 400);
	Graph->RegisterStaticObject(601, StoredRoom, 600);
	StoredShip->Cells.Add(StoredRoom);
	StoredRoom->AttachToActor(StoredShip, FAttachmentTransformRules::KeepRelativeTransform);
	StoredRoom->SetActorHiddenInGame(true); StoredRoom->SetActorEnableCollision(false);
	Graph->RefreshSpaceFrame();
	TestTrue(TEXT("stored ship room cannot render over the active interior"), StoredRoom->IsHidden());
	TestFalse(TEXT("stored ship room cannot collide or report overlaps"), StoredRoom->GetActorEnableCollision());
	TestTrue(TEXT("launched remote ship room remains collidable"), RemoteCell->GetActorEnableCollision());
	ASWGItem* LateStoredItem = World->SpawnActor<ASWGItem>();
	if (!LateStoredItem) return false;
	StoredRoom->InteriorActors.Add(LateStoredItem);
	LateStoredItem->SetActorHiddenInGame(false); LateStoredItem->SetActorEnableCollision(true);
	Graph->RefreshSpaceFrame();
	TestTrue(TEXT("late generated stored interior is suppressed"), LateStoredItem->IsHidden());
	TestFalse(TEXT("late generated stored collision is suppressed"), LateStoredItem->GetActorEnableCollision());
	Graph->ReadyObjects.Add(601);
	Graph->ContainerByObjectId.Add(600, 0);
	Graph->RefreshSpaceFrame();
	TestFalse(TEXT("launch restores stored room visibility"), StoredRoom->IsHidden());
	TestTrue(TEXT("launch restores stored room collision"), StoredRoom->GetActorEnableCollision());
	TestFalse(TEXT("launch restores generated interior visibility"), LateStoredItem->IsHidden());
	Graph->ContainerByObjectId.Add(500, 501); Graph->ContainerByObjectId.Add(501, 500);
	TestNull(TEXT("cyclic containment is bounded"), Graph->FindContainingShip(500));
	Graph->ContainerByObjectId.Add(100, 400);
	Graph->ContainmentTypeByObjectId.Add(100, 4);
	Graph->ContainerByObjectId.Add(400, 103);
	TestEqual(TEXT("stored ship resolves owned control device"), USWGShipLaunchSubsystem::ResolveControlDevice(*Graph, 100), int64(400));
	Graph->ContainerByObjectId.Add(400, 999);
	TestEqual(TEXT("another player's device is rejected"), USWGShipLaunchSubsystem::ResolveControlDevice(*Graph, 100), int64(0));
	Graph->ContainerByObjectId.Add(100, 0);
	TestEqual(TEXT("launched ship cannot be launched again"), USWGShipLaunchSubsystem::ResolveControlDevice(*Graph, 100), int64(0));
	Graph->RemoveObject(100);
	TestFalse(TEXT("ship movement ID removed on destruction"), Graph->ShipObjectIds.Contains(42));
	TestFalse(TEXT("space pose removed on destruction"), Graph->SpaceObjectPoses.Contains(100));
	ASWGPlayer* Pilot = World->SpawnActor<ASWGPlayer>();
	USWGObjectGraphSubsystem* LiveGraph = Game->GetSubsystem<USWGObjectGraphSubsystem>();
	if (!Pilot || !LiveGraph) return false;
	LiveGraph->RegisterStaticObject(104, Pilot);
	LiveGraph->LocalPlayerObjectId = 104; LiveGraph->bSpaceScene = true;
	LiveGraph->ContainerByObjectId.Add(105, 104);
	TestEqual(TEXT("PLAY aliases the current body"), LiveGraph->ResolvePlayerObjectActor(105), static_cast<AActor*>(Pilot));
	LiveGraph->SetCrcToActorClassMap({{0x12345678, ASWGCreature::StaticClass()}});
	LiveGraph->ObjectCrcById.Add(104, 0x12345678);
	FSWGPacket DuplicatePacket;
	DuplicatePacket.WriteUInt16(5); DuplicatePacket.WriteUInt32(uint32(ESWGMessageOp::SceneCreateObjectByCrc));
	DuplicatePacket.WriteUInt64(104);
	for (float Component : {0.f,0.f,0.f,1.f,0.f,0.f,0.f}) DuplicatePacket.WriteFloat(Component);
	DuplicatePacket.WriteUInt32(0x12345678); DuplicatePacket.WriteByte(0);
	FSWGMessage DuplicateReader(DuplicatePacket);
	FSceneCreateObjectMessage Duplicate(uint32(ESWGMessageOp::SceneCreateObjectByCrc), DuplicateReader);
	LiveGraph->HandleSceneCreateObject(Duplicate);
	TestEqual(TEXT("duplicate CREO preserves body actor"), LiveGraph->FindActor(104), static_cast<AActor*>(Pilot));
	TestEqual(TEXT("duplicate CREO preserves PLAY routing"), LiveGraph->FindActor(105), static_cast<AActor*>(Pilot));
	ASWGShip* RepeatedShip = World->SpawnActor<ASWGShip>();
	ASWGCell* RepeatedCell = World->SpawnActor<ASWGCell>();
	if (!RepeatedShip || !RepeatedCell) return false;
	LiveGraph->RegisterStaticObject(120, RepeatedShip);
	LiveGraph->RegisterStaticObject(121, RepeatedCell, 120);
	RepeatedCell->OwningBuilding = RepeatedShip;
	RepeatedShip->Cells.Add(RepeatedCell);
	RepeatedCell->AttachToActor(RepeatedShip, FAttachmentTransformRules::KeepRelativeTransform);
	LiveGraph->SetCrcToActorClassMap({{0x12345678, ASWGCreature::StaticClass()}, {0x12345679, ASWGShip::StaticClass()}, {0x12345680, ASWGCell::StaticClass()}, {0x12345681, ASWGItem::StaticClass()}});
	LiveGraph->ObjectCrcById.Add(120, 0x12345679);
	LiveGraph->ObjectCrcById.Add(121, 0x12345680);
	auto RepeatCreate = [&](int64 Id, uint32 Crc, float X)
	{
		FSWGPacket Packet;
		Packet.WriteUInt16(5); Packet.WriteUInt32(uint32(ESWGMessageOp::SceneCreateObjectByCrc)); Packet.WriteUInt64(Id);
		for (float Component : {0.f,0.f,0.f,1.f,X,0.f,0.f}) Packet.WriteFloat(Component);
		Packet.WriteUInt32(Crc); Packet.WriteByte(0);
		FSWGMessage Reader(Packet);
		FSceneCreateObjectMessage Create(uint32(ESWGMessageOp::SceneCreateObjectByCrc), Reader);
		LiveGraph->HandleSceneCreateObject(Create);
	};
	RepeatCreate(120, 0x12345679, 10.f);
	RepeatCreate(121, 0x12345680, 0.f);
	TestEqual(TEXT("repeated ship create preserves hull actor"), LiveGraph->FindActor(120), static_cast<AActor*>(RepeatedShip));
	TestEqual(TEXT("repeated cell create preserves room actor"), LiveGraph->FindActor(121), static_cast<AActor*>(RepeatedCell));
	TestEqual(TEXT("repeated room still belongs to the canonical hull"), RepeatedCell->OwningBuilding.Get(), static_cast<ASWGBuilding*>(RepeatedShip));
	TestEqual(TEXT("repeated room keeps its mesh attachment"), RepeatedCell->GetAttachParentActor(), static_cast<AActor*>(RepeatedShip));
	TestEqual(TEXT("repeated create does not add a second room"), RepeatedShip->Cells.Num(), 1);
	TestTrue(TEXT("preserved hull takes the new launch pose"), RepeatedShip->SpacePose.GetLocation().Equals(SWGToUnrealSpace(FVector(10,0,0))));
	RepeatedShip->bPredictingLocalFlight = true;
	RepeatCreate(120, 0x12345679, 20.f);
	TestTrue(TEXT("repeated owner create cannot rewind pilot prediction"), RepeatedShip->SpacePose.GetLocation().Equals(SWGToUnrealSpace(FVector(10,0,0))));
	Pilot->SetActorHiddenInGame(false);
	LiveGraph->ApplyContainment(Pilot, 105, 104);
	TestFalse(TEXT("PLAY containment cannot hide the body"), Pilot->IsHidden());
	TestFalse(TEXT("PLAY containment cannot mount the body"), Pilot->RiddenMount.IsValid());
	LiveGraph->ContainmentTypeByObjectId.Add(104, int32(ESWGContainmentType::ShipPilotPOB));
	Pilot->SetSceneLoadingLocked(false);
	TestEqual(TEXT("releasing loading lock keeps pilot character inert"), Pilot->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	LiveGraph->ContainmentTypeByObjectId.Remove(104);
	Pilot->SetSceneLoadingLocked(false);
	TestEqual(TEXT("nonpilot regains ordinary walking after loading"), Pilot->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
	ASWGShip* InputShip = World->SpawnActor<ASWGShip>();
	if (!InputShip) return false;
	LiveGraph->RegisterStaticObject(106, InputShip);
	LiveGraph->ContainerByObjectId.Add(104, 106);
	LiveGraph->ContainmentTypeByObjectId.Add(104, int32(ESWGContainmentType::ShipPilot));
	LiveGraph->ReadyObjects.Add(104); LiveGraph->ReadyObjects.Add(106);
	UStaticMeshComponent* InputHull = NewObject<UStaticMeshComponent>(InputShip);
	InputHull->SetupAttachment(InputShip->GetRootComponent());
	InputHull->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))); InputHull->RegisterComponent();
	InputShip->TangibleComponent->bHasBase3 = true;
	InputShip->ShipState.Flight = FShipFlightLimits{20,40,FVector(2),FVector(1),100};
	if (!Controller->PlayerInput) Controller->PlayerInput = NewObject<UPlayerInput>(Controller);
	Controller->Possess(Pilot);
	Controller->PlayerInput->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::W, IE_Pressed, FPlatformTime::Cycles64()));
	Controller->PlayerInput->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::D, IE_Pressed, FPlatformTime::Cycles64()));
	Controller->PlayerInput->ProcessInputStack({}, .1f, false);
	TestTrue(TEXT("pilot input is consumed while waiting for readiness"), Pilot->TickPilotShip(.1f));
	TestTrue(TEXT("waiting for baselines cannot drive ship"), InputShip->SpacePose.Equals(FTransform::Identity));
	InputShip->bHasFlightBaseline = true;
	TestTrue(TEXT("pilot input routes to flight"), Pilot->TickPilotShip(.1f));
	TestTrue(TEXT("W key accelerates the ship"), InputShip->SpaceVelocity.Size() > 0);
	TestTrue(TEXT("HUD throttle follows real W input"), Pilot->GetShipThrottle() > 0);
	TestTrue(TEXT("D key turns ship yaw"), InputShip->FlightAngularRates.Y > 0);
	TestTrue(TEXT("input keeps interior hull fixed"), InputShip->GetActorTransform().Equals(FTransform::Identity));
	TestTrue(TEXT("entering pilot control defaults to third-person ship view"), Pilot->IsExteriorShipView());
	Pilot->OnGamepadView();
	TestFalse(TEXT("pilot can choose cockpit after default exterior view"), Pilot->IsExteriorShipView());
	Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0));
	Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::D, IE_Released, 0));
	auto PadAxis = [&](FKey Key, float Value)
	{
		Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Axis, Value, 1));
	};
	FInputAxisProperties StickProperties;
	StickProperties.DeadZone = .25f;
	Controller->PlayerInput->SetAxisProperties(EKeys::Gamepad_LeftX, StickProperties);
	Controller->PlayerInput->SetAxisProperties(EKeys::Gamepad_LeftY, StickProperties);
	PadAxis(EKeys::Gamepad_LeftX, .1f); PadAxis(EKeys::Gamepad_LeftY, -.1f);
	Controller->PlayerInput->ProcessInputStack({}, .1f, false);
	const float NeutralThrottle = Pilot->GetShipThrottle();
	Pilot->TickPilotShip(.1f);
	TestTrue(TEXT("stick drift is removed by Unreal dead zones"), InputShip->FlightAngularRates.IsNearlyZero());
	TestEqual(TEXT("neutral pad retains throttle"), Pilot->GetShipThrottle(), NeutralThrottle);
	const FQuat BeforeBank = InputShip->SpacePose.GetRotation();
	PadAxis(EKeys::Gamepad_LeftX, .625f); PadAxis(EKeys::Gamepad_LeftY, .625f);
	PadAxis(EKeys::Gamepad_RightTriggerAxis, .5f);
	Controller->PlayerInput->ProcessInputStack({}, .1f, false);
	Pilot->TickPilotShip(.1f);
	TestTrue(TEXT("left stick banks without yaw"), InputShip->FlightAngularRates.Z < 0 && FMath::IsNearlyZero(InputShip->FlightAngularRates.Y));
	TestTrue(TEXT("stick right lowers the right wing"), (BeforeBank.Inverse() * InputShip->SpacePose.GetRotation()).RotateVector(FVector::RightVector).Z < 0);
	PadAxis(EKeys::Gamepad_RightTriggerAxis, 0);
	Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_RightShoulder, IE_Pressed, 1));
	Controller->PlayerInput->ProcessInputStack({}, .1f, false);
	Pilot->TickPilotShip(.1f);
	TestTrue(TEXT("left stick steers pitch and roll proportionally"), InputShip->FlightAngularRates.X > 0 && InputShip->FlightAngularRates.Z < 0);
	TestTrue(TEXT("right bumper yaws ship"), InputShip->FlightAngularRates.Y > 0);
	TestTrue(TEXT("right trigger raises throttle proportionally"), FMath::IsNearlyEqual(Pilot->GetShipThrottle(), NeutralThrottle + .025f));
	const float PadThrottle = Pilot->GetShipThrottle();
	Controller->SetIgnoreMoveInput(true);
	Pilot->TickPilotShip(.25f);
	TestEqual(TEXT("modal input lock blocks controller throttle"), Pilot->GetShipThrottle(), PadThrottle);
	TestTrue(TEXT("modal input lock blocks controller steering"), InputShip->FlightAngularRates.IsNearlyZero());
	Controller->SetIgnoreMoveInput(false);
	PadAxis(EKeys::Gamepad_LeftX, 0); PadAxis(EKeys::Gamepad_LeftY, 0);
	PadAxis(EKeys::Gamepad_RightTriggerAxis, 0); PadAxis(EKeys::Gamepad_LeftTriggerAxis, .5f);
	Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_RightShoulder, IE_Released, 0));
	Controller->PlayerInput->ProcessInputStack({}, .1f, false);
	Pilot->TickPilotShip(.1f);
	TestTrue(TEXT("left trigger lowers throttle proportionally"), FMath::IsNearlyEqual(Pilot->GetShipThrottle(), NeutralThrottle));
	PadAxis(EKeys::Gamepad_LeftTriggerAxis, 0);
	PadAxis(EKeys::Gamepad_RightX, .75f); PadAxis(EKeys::Gamepad_RightY, .75f);
	Controller->PlayerInput->ProcessInputStack({}, .1f, false);
	FMinimalViewInfo LookViewBefore; Pilot->CalcCamera(.1f, LookViewBefore);
	const FRotator LookBefore = Pilot->ShipCameraLook;
	Pilot->TickPilotShip(.1f);
	TestTrue(TEXT("right stick moves ship camera yaw and pitch"), Pilot->ShipCameraLook.Yaw > LookBefore.Yaw && Pilot->ShipCameraLook.Pitch > LookBefore.Pitch);
	TestTrue(TEXT("right stick does not steer ship"), InputShip->FlightAngularRates.IsNearlyZero());
	FMinimalViewInfo LookViewAfter; Pilot->CalcCamera(.1f, LookViewAfter);
	TestFalse(TEXT("right stick changes rendered cockpit direction"), LookViewAfter.Rotation.Equals(LookViewBefore.Rotation));
	TestTrue(TEXT("cockpit look retains seated eye position"), LookViewAfter.Location.Equals(LookViewBefore.Location));
	const FRotator CameraLook = Pilot->ShipCameraLook;
	Controller->SetIgnoreLookInput(true);
	Pilot->TickPilotShip(.1f);
	TestTrue(TEXT("look input lock freezes ship camera"), Pilot->ShipCameraLook.Equals(CameraLook));
	Controller->SetIgnoreLookInput(false);
	Pilot->ShipCameraLook.Pitch = 79.9f;
	Pilot->TickPilotShip(.1f);
	TestEqual(TEXT("camera pitch remains bounded"), Pilot->ShipCameraLook.Pitch, 80.0);
	PadAxis(EKeys::Gamepad_RightX, 0); PadAxis(EKeys::Gamepad_RightY, 0);
	Controller->PlayerInput->ProcessInputStack({}, .1f, false);
	Pilot->bActionBankShifted = false;
	Pilot->bZoomModifierHeld = false;
	Pilot->ToggleActionBank(); Pilot->OnZoomModifierPressed();
	TestFalse(TEXT("flight brake does not switch action banks"), Pilot->bActionBankShifted);
	TestFalse(TEXT("flight throttle does not enable camera zoom"), Pilot->bZoomModifierHeld);
	Pilot->OnGamepadView();
	TestTrue(TEXT("controller view button selects exterior"), Pilot->IsExteriorShipView());
	TestTrue(TEXT("view change recenters camera"), Pilot->ShipCameraLook.IsZero());
	FMinimalViewInfo OrbitBefore; Pilot->CalcCamera(.1f, OrbitBefore);
	PadAxis(EKeys::Gamepad_RightX, .75f);
	Controller->PlayerInput->ProcessInputStack({}, .1f, false);
	Pilot->TickPilotShip(.1f);
	FMinimalViewInfo OrbitAfter; Pilot->CalcCamera(.1f, OrbitAfter);
	TestFalse(TEXT("right stick orbits rendered exterior camera"), OrbitAfter.Location.Equals(OrbitBefore.Location));
	TestTrue(TEXT("camera orbit keeps the ship interior fixed"), InputShip->GetActorTransform().Equals(FTransform::Identity));
	PadAxis(EKeys::Gamepad_RightX, 0);
	Controller->PlayerInput->ProcessInputStack({}, .1f, false);
	Pilot->OnGamepadView();
	TestFalse(TEXT("controller view button restores cockpit"), Pilot->IsExteriorShipView());
	PadAxis(EKeys::Gamepad_LeftTriggerAxis, 0);
	Controller->PlayerInput->ProcessInputStack({}, .1f, false);
	const FTransform DrivenPose = InputShip->SpacePose;
	FMinimalViewInfo Cockpit;
	Pilot->CalcCamera(.1f, Cockpit);
	TestTrue(TEXT("cockpit hides only own hull primitives"), Controller->HiddenPrimitiveComponents.Contains(InputHull));
	TestFalse(TEXT("cockpit does not hide the ship actor and its attached rooms"), Controller->HiddenActors.Contains(InputShip));
	TestTrue(TEXT("cockpit follows authored pilot parent"), Cockpit.Location.Equals(InputShip->GetActorTransform().TransformPosition(FVector(0,0,SWGWorldScale)), .01));
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	if (!Tre) return false;
	Tre->LoadConfig();
	if (!TestTrue(TEXT("camera fixture loads actual archives"), Tre->LoadArchives())) return false;
	FSWGCockpitData AuthoredCamera;
	if (!TestTrue(TEXT("camera fixture loads real yacht CPIT"), FSWGCockpitReader::Read(
		Tre->CreateIffReader(TEXT("cockpit/sorosuub_space_yacht.iff")), AuthoredCamera))) return false;
	InputShip->CockpitData = AuthoredCamera;
	const FTransform PilotBeforeCamera = Pilot->GetActorTransform();
	const FVector CameraFeet(200,100,50);
	Pilot->SetActorLocation(CameraFeet + FVector(0,0,Pilot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	Pilot->SetActorRotation(FRotator(0,45,0));
	Pilot->CalcCamera(.1f, Cockpit);
	TestTrue(TEXT("real yacht cockpit uses character feet plus authored eye offset"), Cockpit.Location.Equals(CameraFeet + FVector(0,0,.7 * SWGWorldScale), .01));
	TestTrue(TEXT("configured cockpit retains character orientation"), Cockpit.Rotation.Equals(Pilot->GetActorRotation(), .01));
	InputShip->CockpitData->NativeFirstPersonOffset = FVector3f(.4f,-.5f,-.7f);
	Pilot->CalcCamera(.1f, Cockpit);
	TestTrue(TEXT("native camera offset axes rotate in the character frame"), Cockpit.Location.Equals(
		CameraFeet + Pilot->GetActorQuat().RotateVector(FVector(-.7 * SWGWorldScale,.4 * SWGWorldScale,.7 * SWGWorldScale)), .01));
	InputShip->CockpitData = AuthoredCamera;
	Pilot->SetActorTransform(PilotBeforeCamera);
	Pilot->CalcCamera(.1f, Cockpit);
	ASWGCell* CameraRoom = World->SpawnActor<ASWGCell>();
	InputShip->Cells.Add(CameraRoom);
	CameraRoom->AttachToActor(InputShip, FAttachmentTransformRules::KeepWorldTransform);
	UStaticMeshComponent* RoomPrimitive = NewObject<UStaticMeshComponent>(CameraRoom);
	RoomPrimitive->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	CameraRoom->SetRootComponent(RoomPrimitive); RoomPrimitive->RegisterComponent();
	RoomPrimitive->AttachToComponent(InputHull, FAttachmentTransformRules::KeepWorldTransform);
	TSet<FPrimitiveComponentId> HiddenPrimitives;
	Controller->BuildHiddenComponentList(Cockpit.Location, HiddenPrimitives);
	TestTrue(TEXT("Unreal hides the hull in cockpit view"), HiddenPrimitives.Contains(InputHull->GetPrimitiveSceneId()));
	TestFalse(TEXT("Unreal keeps a room primitive attached to the hull visible"), HiddenPrimitives.Contains(RoomPrimitive->GetPrimitiveSceneId()));
	const FTransform RoomBefore = CameraRoom->GetActorTransform();
	Pilot->ToggleShipView();
	FMinimalViewInfo Exterior;
	Pilot->CalcCamera(.1f, Exterior);
	TestFalse(TEXT("exterior restores own hull visibility"), Controller->HiddenPrimitiveComponents.Contains(InputHull));
	TestFalse(TEXT("exterior defeats room-trigger shell hiding"), InputShip->IsHidden());
	TestTrue(TEXT("exterior hides room geometry for this view"), Controller->HiddenActors.Contains(CameraRoom));
	TestTrue(TEXT("exterior frames hull from outside"), FVector::Distance(Exterior.Location, InputShip->GetActorLocation()) > 200);
	const FVector AuthoredExteriorOffset = FRotator(-11.25, 0, 0).RotateVector(FVector(-61 * ASWGPlayer::GetShipZoomMultiplier() * SWGWorldScale, 0, 0));
	TestTrue(TEXT("yacht exterior uses widest authored exterior zoom about ship origin"), Exterior.Location.Equals(InputShip->GetActorLocation() + AuthoredExteriorOffset, .01));
	TestTrue(TEXT("yacht exterior uses retail downward pitch"), Exterior.Rotation.Equals(FRotator(-11.25, 0, 0), .01));
	InputShip->CockpitData->ZoomDistances = {0, 5, 40, 10};
	FMinimalViewInfo FilteredZoom; Pilot->CalcCamera(1.f, FilteredZoom);
	TestTrue(TEXT("exterior skips authored distances below FRST"), FMath::IsNearlyEqual(FVector::Distance(FilteredZoom.Location, InputShip->GetActorLocation()), 40 * ASWGPlayer::GetShipZoomMultiplier() * SWGWorldScale, .01));
	InputShip->CockpitData = AuthoredCamera;
	TestTrue(TEXT("view toggle preserves server containment"), LiveGraph->IsLocalPlayerPiloting());
	TestTrue(TEXT("view toggle preserves ship flight pose"), InputShip->SpacePose.Equals(DrivenPose));
	TestTrue(TEXT("view toggle preserves room collision frame"), CameraRoom->GetActorTransform().Equals(RoomBefore));
	InputShip->IntegrateFlight(.1f, .5f, FVector(0,0,1));
	LiveGraph->RefreshSpaceFrame();
	FMinimalViewInfo RolledExterior;
	Pilot->CalcCamera(1.f, RolledExterior);
	TestTrue(TEXT("exterior camera remains ship-relative during flight and roll"), RolledExterior.Location.Equals(Exterior.Location, .01)
		&& RolledExterior.Rotation.Equals(Exterior.Rotation, .01));
	TestTrue(TEXT("HUD reports exterior view"), Pilot->IsExteriorShipView());
	TestTrue(TEXT("room remains fixed during flight and roll"), CameraRoom->GetActorTransform().Equals(RoomBefore));
	{
		// Pilot camera zoom: the multiplier, wheel and D-pad steps, the limits, and the retail min(1, 5*dt) easing.
		IConsoleVariable* ZoomMultiplier = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.ShipCameraZoomMultiplier"));
		if (!TestNotNull(TEXT("zoom multiplier cvar exists"), ZoomMultiplier)) return false;
		const float SavedZoomMultiplier = ZoomMultiplier->GetFloat();
		ON_SCOPE_EXIT { ZoomMultiplier->Set(SavedZoomMultiplier, ECVF_SetByCode); };
		auto CameraMetres = [&](float DeltaTime)
		{
			FMinimalViewInfo View; Pilot->CalcCamera(DeltaTime, View);
			return float(FVector::Dist(View.Location, InputShip->GetActorLocation()) / SWGWorldScale);
		};
		TestTrue(TEXT("default zoom multiplier puts the camera beyond the widest retail stop"), SavedZoomMultiplier > 1.f);
		ZoomMultiplier->Set(1.f, ECVF_SetByCode);
		Pilot->ShipZoomOffset = 0;
		TestTrue(TEXT("multiplier one gives the widest authored stop"), FMath::IsNearlyEqual(CameraMetres(1.f), 61.f, .01f));
		ZoomMultiplier->Set(2.f, ECVF_SetByCode);
		TestTrue(TEXT("multiplier scales the authored stops"), FMath::IsNearlyEqual(CameraMetres(1.f), 122.f, .01f));
		ZoomMultiplier->Set(.25f, ECVF_SetByCode);
		TestTrue(TEXT("multiplier is floored at one"), FMath::IsNearlyEqual(CameraMetres(1.f), 61.f, .01f));
		ZoomMultiplier->Set(1.f, ECVF_SetByCode);
		Pilot->OnMouseWheel(-1.f);
		TestEqual(TEXT("wheel back selects the next stop out"), Pilot->ShipZoomOffset, 1);
		TestTrue(TEXT("camera eases a fifth of the way per fifth of a second"), FMath::IsNearlyEqual(CameraMetres(.1f), 61.f + 15.25f * .5f, .01f));
		TestTrue(TEXT("camera keeps easing toward the stop"), FMath::IsNearlyEqual(CameraMetres(.1f), 61.f + 15.25f * .75f, .01f));
		TestTrue(TEXT("a long frame lands on the stop"), FMath::IsNearlyEqual(CameraMetres(1.f), 76.25f, .01f));
		Pilot->OnMouseWheel(1.f);
		TestEqual(TEXT("wheel forward steps back in"), Pilot->ShipZoomOffset, 0);
		for (int32 Notch = 0; Notch < 40; ++Notch) Pilot->OnMouseWheel(-1.f);
		TestTrue(TEXT("wheel stops at three times the widest stop"), FMath::IsNearlyEqual(CameraMetres(1.f), 183.f, .01f));
		Pilot->OnMouseWheel(-1.f);
		TestTrue(TEXT("further wheel does not pass the maximum"), FMath::IsNearlyEqual(CameraMetres(1.f), 183.f, .01f));
		for (int32 Notch = 0; Notch < 40; ++Notch) Pilot->OnMouseWheel(1.f);
		TestTrue(TEXT("wheel stops at the closest exterior stop, outside first person"), FMath::IsNearlyEqual(CameraMetres(1.f), 35.f, .01f));
		Pilot->ShipZoomOffset = 0;

		// Controller: the D-pad, free while piloting because the action bar is hidden. Down zooms out, up zooms in.
		UInputComponent* PadInput = NewObject<UEnhancedInputComponent>(Pilot);
		Pilot->SetupPlayerInputComponent(PadInput);
		int32 ZoomKeyBindings = 0;
		for (const FInputKeyBinding& Binding : PadInput->KeyBindings)
			ZoomKeyBindings += (Binding.Chord.Key == EKeys::Gamepad_DPad_Up || Binding.Chord.Key == EKeys::Gamepad_DPad_Down) && Binding.KeyEvent == IE_Pressed;
		TestTrue(TEXT("D-pad up and down are bound for zoom (alongside the action-bar slots)"), ZoomKeyBindings >= 4);
		auto PressPadKey = [&](const FKey& Key)
		{
			Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 0));
			Controller->PlayerInput->ProcessInputStack({PadInput}, .1f, false);
			Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0));
		};
		PressPadKey(EKeys::Gamepad_DPad_Down);
		TestEqual(TEXT("D-pad down zooms the pilot camera out"), Pilot->ShipZoomOffset, 1);
		PressPadKey(EKeys::Gamepad_DPad_Up);
		PressPadKey(EKeys::Gamepad_DPad_Up);
		TestEqual(TEXT("D-pad up zooms the pilot camera in"), Pilot->ShipZoomOffset, -1);
		Pilot->ShipZoomOffset = 0;
		ZoomMultiplier->Set(SavedZoomMultiplier, ECVF_SetByCode);
		CameraMetres(1.f);
	}
	Pilot->ToggleShipView();
	Pilot->CalcCamera(.1f, Cockpit);
	TestFalse(TEXT("cockpit restores room visibility"), Controller->HiddenActors.Contains(CameraRoom));
	TestTrue(TEXT("authored cockpit restored after view toggle"), Cockpit.Location.Equals(Pilot->GetActorLocation()
		- FVector(0,0,Pilot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) + FVector(0,0,.7 * SWGWorldScale), .01));
	TestFalse(TEXT("HUD reports cockpit view"), Pilot->IsExteriorShipView());
	const FTransform FinalFlightPose = InputShip->SpacePose;
	LiveGraph->ContainmentTypeByObjectId.Remove(104);
	TestFalse(TEXT("nonpilot input is released to walking"), Pilot->TickPilotShip(.1f));
	Pilot->ToggleActionBank(); Pilot->OnZoomModifierPressed();
	TestTrue(TEXT("walking restores controller action bank"), Pilot->bActionBankShifted);
	TestTrue(TEXT("walking restores controller zoom modifier"), Pilot->bZoomModifierHeld);
	Pilot->OnZoomModifierReleased();
	{
		const float ArmBefore = Pilot->CameraBoom->TargetArmLength;
		const int32 ZoomOffsetBefore = Pilot->ShipZoomOffset;
		Pilot->OnMouseWheel(1.f);
		TestTrue(TEXT("walking wheel still zooms the spring arm"), Pilot->CameraBoom->TargetArmLength < ArmBefore);
		TestEqual(TEXT("walking wheel leaves the pilot zoom alone"), Pilot->ShipZoomOffset, ZoomOffsetBefore);
		Pilot->OnShipZoomOut();
		TestEqual(TEXT("walking D-pad does not zoom the pilot camera"), Pilot->ShipZoomOffset, ZoomOffsetBefore);
	}
	TestTrue(TEXT("nonpilot cannot drive the ship"), InputShip->SpacePose.Equals(FinalFlightPose));
	TestFalse(TEXT("unpilot restores ordinary hull visibility"), Controller->HiddenPrimitiveComponents.Contains(InputHull));
	Pilot->ToggleShipView();
	TestFalse(TEXT("nonpilot cannot enter exterior pilot view"), Pilot->bExteriorShipView);
	LiveGraph->RegisterStaticObject(108, CameraRoom, 106);
	CameraRoom->OwningBuilding = InputShip;
	CameraRoom->bCollisionReady = true;
	ASWGItem* ReseatChair = World->SpawnActor<ASWGItem>();
	if (!ReseatChair) return false;
	LiveGraph->RegisterStaticObject(109, ReseatChair, 108);
	ReseatChair->AttachToActor(CameraRoom, FAttachmentTransformRules::KeepRelativeTransform);
	ReseatChair->SetActorRelativeLocation(FVector(120,30,10));
	LiveGraph->ReadyObjects.Add(109);
	auto ConfirmContainment = [&](int64 Parent, ESWGContainmentType Type)
	{
		FSWGPacket Packet; Packet.WriteUInt16(4); Packet.WriteUInt32(uint32(ESWGMessageOp::UpdateContainmentMessage));
		Packet.WriteUInt64(104); Packet.WriteUInt64(Parent); Packet.WriteUInt32(uint32(Type));
		FSWGMessage Reader(Packet);
		FUpdateContainmentMessage Update(uint32(ESWGMessageOp::UpdateContainmentMessage), Reader);
		LiveGraph->HandleUpdateContainment(Update);
	};
	ConfirmContainment(109, ESWGContainmentType::ShipPilotPOB);
	LiveGraph->Tick(.016f);
	TestTrue(TEXT("confirmed reseat places body in chair frame"), Pilot->GetActorLocation().Equals(
		ReseatChair->GetActorTransform().TransformPosition(FVector(0,0,50 + Pilot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())), .01));
	TestEqual(TEXT("confirmed reseat disables character movement"), Pilot->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	Pilot->TickPilotShip(.1f);
	TestTrue(TEXT("confirmed reseat regains flight prediction"), InputShip->bPredictingLocalFlight);
	TestTrue(TEXT("reseating restores third-person ship view"), Pilot->IsExteriorShipView());
	Pilot->OnGamepadInteract();
	TestTrue(TEXT("exit request cannot fabricate unpilot containment"), LiveGraph->IsLocalPlayerPiloting());
	TestEqual(TEXT("exit request cannot unlock walking before confirmation"), Pilot->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	CameraRoom->bCollisionReady = false;
	ConfirmContainment(108, ESWGContainmentType::VolumeContained);
	TestTrue(TEXT("confirmed exit matches server chair feet position"), Pilot->GetActorLocation().Equals(
		ReseatChair->GetActorLocation() + FVector(0,0,Pilot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), .01));
	LiveGraph->Tick(.016f);
	TestEqual(TEXT("confirmed exit waits for room collision"), Pilot->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	CameraRoom->bCollisionReady = true;
	LiveGraph->Tick(.016f);
	TestEqual(TEXT("confirmed exit restores walking after collision"), Pilot->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
	TestTrue(TEXT("confirmed exit restores character collision"), Pilot->GetActorEnableCollision());
	Pilot->TickPilotShip(.1f);
	TestFalse(TEXT("confirmed exit releases flight prediction"), InputShip->bPredictingLocalFlight);
	ASWGShip* OverlappingStoredShip = World->SpawnActor<ASWGShip>();
	ASWGCell* WrongRoom = World->SpawnActor<ASWGCell>();
	if (!OverlappingStoredShip || !WrongRoom) return false;
	LiveGraph->RegisterStaticObject(110, OverlappingStoredShip, 400);
	LiveGraph->RegisterStaticObject(111, WrongRoom, 110);
	WrongRoom->OwningBuilding = OverlappingStoredShip;
	Pilot->DispatchBeginPlay(); WrongRoom->DispatchBeginPlay();
	UBoxComponent* WrongTrigger = NewObject<UBoxComponent>(WrongRoom);
	WrongTrigger->SetupAttachment(WrongRoom->GetRootComponent());
	WrongTrigger->SetBoxExtent(FVector(1000));
	WrongTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WrongTrigger->SetCollisionObjectType(ECC_WorldDynamic);
	WrongTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	WrongTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	WrongTrigger->RegisterComponent(); WrongTrigger->UpdateOverlaps();
	Pilot->GetCapsuleComponent()->UpdateOverlaps();
	TArray<AActor*> WrongOverlaps;
	Pilot->GetCapsuleComponent()->GetOverlappingActors(WrongOverlaps, ASWGCell::StaticClass());
	TestTrue(TEXT("room-report regression has an overlapping foreign cell"), WrongOverlaps.Contains(WrongRoom));
	CameraRoom->TriggerVolume = NewObject<UBoxComponent>(CameraRoom);
	TestEqual(TEXT("foreign overlap cannot report entry into another ship"), Pilot->ResolveCurrentCell(Pilot), CameraRoom);
	WrongTrigger->DestroyComponent(); CameraRoom->TriggerVolume = nullptr;
	UCameraComponent* WalkingCamera = Pilot->FindComponentByClass<UCameraComponent>();
	if (!WalkingCamera) return false;
	const FTransform WalkingCameraBefore = WalkingCamera->GetComponentTransform();
	WalkingCamera->SetWorldLocation(Pilot->GetActorLocation());
	FMinimalViewInfo WalkingView;
	Pilot->CalcCamera(.1f, WalkingView);
	TestTrue(TEXT("compressed walking camera hides the local body"), Controller->HiddenActors.Contains(Pilot));
	TestTrue(TEXT("compressed walking camera keeps the room visible"), !Controller->HiddenActors.Contains(CameraRoom));
	WalkingCamera->SetWorldLocation(Pilot->GetActorLocation() + FVector(-300,0,0));
	Pilot->CalcCamera(.1f, WalkingView);
	TestFalse(TEXT("walking body returns when the camera has clearance"), Controller->HiddenActors.Contains(Pilot));
	WalkingCamera->SetWorldTransform(WalkingCameraBefore);
	Pilot->SetActorLocation(FVector(500,400,200));
	ConfirmContainment(109, ESWGContainmentType::ShipPilotPOB);
	LiveGraph->Tick(.016f);
	TestTrue(TEXT("reseat uses chair coordinates rather than the walking position"), Pilot->GetActorLocation().Equals(
		ReseatChair->GetActorTransform().TransformPosition(FVector(0,0,50 + Pilot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())), .01));
	LiveGraph->ObjectCrcById.Add(109, 0x12345681);
	const FTransform ChairBeforeRepeat = ReseatChair->GetActorTransform();
	RepeatCreate(109, 0x12345681, 123.f);
	TestEqual(TEXT("repeated chair create retains the canonical furniture actor"), LiveGraph->FindActor(109), static_cast<AActor*>(ReseatChair));
	TestTrue(TEXT("repeated chair create retains authored placement and room attachment"), ReseatChair->GetActorTransform().Equals(ChairBeforeRepeat)
		&& ReseatChair->GetAttachParentActor() == CameraRoom);
	TestTrue(TEXT("repeated chair create preserves pilot readiness"), LiveGraph->IsLocalSpaceSceneReady());
	LiveGraph->RemoveObject(109);
	TestTrue(TEXT("seat removal preserves server pilot containment"), LiveGraph->IsLocalPlayerPiloting());
	TestFalse(TEXT("missing seat blocks flight readiness"), LiveGraph->IsLocalSpaceSceneReady());
	TestTrue(TEXT("seat removal keeps the controller pawn"), Controller->GetPawn() == Pilot);
	USWGCommandSubsystem* Commands = Game->GetSubsystem<USWGCommandSubsystem>();
	if (!Commands) return false;
	int32 ExitRequests = 0;
	const FDelegateHandle ExitHandle = Commands->OnCommandSent.AddLambda([&](const FString& Name, uint32)
	{
		if (Name == TEXT("leaveStation")) ++ExitRequests;
	});
	ON_SCOPE_EXIT { Commands->OnCommandSent.Remove(ExitHandle); };
	Controller->SetIgnoreMoveInput(true); Pilot->OnGamepadInteract(); Controller->SetIgnoreMoveInput(false);
	TestEqual(TEXT("input lock still blocks missing-seat exit"), ExitRequests, 0);
	Pilot->SetSceneLoadingLocked(true); Pilot->OnGamepadInteract(); Pilot->SetSceneLoadingLocked(false);
	TestEqual(TEXT("scene loading still blocks missing-seat exit"), ExitRequests, 0);
	Pilot->OnGamepadInteract();
	TestEqual(TEXT("controller can request server exit after seat destruction"), ExitRequests, 1);
	TestTrue(TEXT("missing-seat exit cannot fabricate walking containment"), LiveGraph->IsLocalPlayerPiloting());
	TestEqual(TEXT("exit request keeps the character inert until confirmation"), Pilot->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	ConfirmContainment(108, ESWGContainmentType::VolumeContained); LiveGraph->Tick(.016f);
	TestTrue(TEXT("confirmed missing-seat exit recovers the containing ship"), LiveGraph->GetLocalPlayerShip() == InputShip);
	TestTrue(TEXT("confirmed missing-seat exit restores floor readiness"), LiveGraph->IsLocalSpaceSceneReady());
	TestEqual(TEXT("confirmed missing-seat exit restores walking"), Pilot->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
	return true;
}
#endif
