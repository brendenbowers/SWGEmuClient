#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "UObject/GarbageCollection.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SWGPropAttachmentsComponent.h"
#include "Objects/World/SWGShip.h"
#include "Objects/World/SWGStaticProp.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGPropAttachmentsSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Common/SWGWorldScale.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	class FSWGAttachmentCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSWGAttachmentCommand(TFunction<bool()> InUpdate) : UpdateFunction(MoveTemp(InUpdate)) {}
		bool Update() override { return UpdateFunction(); }
	private:
		TFunction<bool()> UpdateFunction;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceAttachmentTest, "SWG.Space.CdfObjectAttachment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

bool FSWGSpaceAttachmentTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->AddToRoot(); Game->InitializeStandalone();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
	World->AddToRoot(); Game->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(Game);
	World->InitializeActorsForPlay(FURL());
	auto Cleanup = [Game, World] { Game->Shutdown(); World->DestroyWorld(false); World->RemoveFromRoot(); Game->RemoveFromRoot(); };
	USWGMeshGeneratorSubsystem* Meshes = Game->GetSubsystem<USWGMeshGeneratorSubsystem>();
	USWGObjectGraphSubsystem* Graph = Game->GetSubsystem<USWGObjectGraphSubsystem>();
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	USWGPropAttachmentsSubsystem* Props = World->GetSubsystem<USWGPropAttachmentsSubsystem>();
	if (!Meshes || !Graph || !Tre || !Props) { Cleanup(); return false; }
	Tre->LoadConfig();
	if (!TestTrue(TEXT("real archives load"), Tre->LoadArchives())) { Cleanup(); return false; }
	TSharedRef<FSWGClientDataFile> Data = MakeShared<FSWGClientDataFile>();
	if (!TestTrue(TEXT("actual Hutt ship CDF loads"), Meshes->ReadClientDataFile(TEXT("object/ship/player/shared_player_hutt_turret_ship.iff"), *Data)))
	{ Cleanup(); return false; }
	const FSWGClientDataChild* Child = Data->Children.FindByPredicate([](const FSWGClientDataChild& Entry)
	{
		return Entry.bObjectTemplate && Entry.Path == TEXT("object/tangible/ship/attachment/wing/shared_hutt_heavy_wing_s01.iff");
	});
	if (!TestNotNull(TEXT("actual wing object child exists"), Child)) { Cleanup(); return false; }
	ASWGShip* Ship = World->SpawnActor<ASWGShip>();
	ASWGShip* OtherShip = World->SpawnActor<ASWGShip>();
	if (!Ship || !OtherShip) { Cleanup(); return false; }
	Graph->RegisterStaticObject(100, Ship); Graph->RegisterStaticObject(200, OtherShip);
	Graph->bSpaceScene = true; Graph->LocalPlayerObjectId = 300; Graph->ContainerByObjectId.Add(300, 200);
	Ship->SetActorTransform(FTransform(FRotator(10,20,30), FVector(2000,3000,4000)));
	USWGPropAttachmentsComponent* Attachments = NewObject<USWGPropAttachmentsComponent>(Ship);
	Attachments->Setup(*Props, Data, true); Attachments->RegisterComponent();
	ASWGStaticProp* Wing = nullptr;
	for (TActorIterator<ASWGStaticProp> It(World); It; ++It)
		if (It->StaticTemplatePath == Child->Path) { Wing = *It; break; }
	if (!TestNotNull(TEXT("real wing child spawns"), Wing)) { Cleanup(); return false; }
	const FTransform ExpectedLocal(FRotator(0,Child->HeadingDegrees,0), SWGFx::SimToUnreal(FVector(Child->Offset)));
	TestTrue(TEXT("CDF placement is preserved before async loading"), Wing->GetActorTransform().Equals(ExpectedLocal * Ship->GetActorTransform(), .01));
	TestTrue(TEXT("pending wing is already parented"), Wing->GetAttachParentActor() == Ship);
	const double Started = FPlatformTime::Seconds();
	AddCommand(new FSWGAttachmentCommand([this, World, Ship, Wing, Meshes, Tre, Graph, Props, Data, ExpectedLocal, Started, Cleanup,
		LateFuture = TSharedPtr<TFuture<FSWGMeshGenerationResult>>(),
		RetiringActor = TWeakObjectPtr<ASWGStaticProp>(), OriginalRoot = TWeakObjectPtr<USceneComponent>()]() mutable
	{
		Meshes->Tick(1.f/60.f);
		if (LateFuture.IsValid())
		{
			if (!LateFuture->IsReady())
			{
				if (FPlatformTime::Seconds()-Started < 60) return false;
				AddError(TEXT("removed-owner mesh completion timed out")); Cleanup(); return true;
			}
			if (RetiringActor.IsValid())
			{
				TestTrue(TEXT("retired subsystem cancels dispatched mesh completion"), LateFuture->Get().MeshOrComponent.IsType<FEmptyVariantState>());
				TestTrue(TEXT("retired completion leaves surviving actor root unchanged"), RetiringActor->GetRootComponent() == OriginalRoot.Get());
				TestFalse(TEXT("retired subsystem cannot tick"), Meshes->IsTickable());
				TestFalse(TEXT("TRE shutdown clears archives after draining consumers"), Tre->IsLoaded());
				Cleanup(); return true;
			}
			TestTrue(TEXT("late mesh completion returns cancellation instead of attaching"), LateFuture->Get().MeshOrComponent.IsType<FEmptyVariantState>());
			int32 Remaining = 0;
			for (TActorIterator<ASWGStaticProp> It(World); It; ++It) ++Remaining;
			TestEqual(TEXT("late completion cannot resurrect an orphan prop"), Remaining, 0);
			RetiringActor = World->SpawnActor<ASWGStaticProp>();
			if (!TestTrue(TEXT("retirement fixture actor survives"), RetiringActor.IsValid())) { Cleanup(); return true; }
			OriginalRoot = RetiringActor->GetRootComponent();
			LateFuture = MakeShared<TFuture<FSWGMeshGenerationResult>>(Meshes->RequestMesh(RetiringActor.Get(), TEXT("appearance/mesh/hutt_fighter_heavy_struct_s01_l0.msh")));
			Meshes->Tick(1.f/60.f);
			TestFalse(TEXT("retirement fixture dispatched before shutdown"), LateFuture->IsReady());
			// Exercise TRE-first shutdown while a real parsing task is in flight.
			Tre->Deinitialize();
			TestFalse(TEXT("TRE-first shutdown stops mesh work before clearing archives"), Meshes->IsTickable());
			return false;
		}
		UStaticMeshComponent* Root = Cast<UStaticMeshComponent>(Wing->GetRootComponent());
		if (!Root || !Root->GetStaticMesh() || !Root->GetStaticMesh()->GetRenderData())
		{
			if (FPlatformTime::Seconds()-Started < 60) return false;
			AddError(TEXT("actual wing mesh completion timed out")); Cleanup(); return true;
		}
		TestTrue(TEXT("generated root retains the ship attachment"), Wing->GetAttachParentActor() == Ship);
		TestEqual(TEXT("wing inherits movable parent mobility"), Root->Mobility.GetValue(), EComponentMobility::Movable);
		TestTrue(TEXT("async root replacement preserves CDF placement"), Wing->GetActorTransform().Equals(ExpectedLocal * Ship->GetActorTransform(), .01));
		Ship->SetActorTransform(FTransform(FRotator(43,55,-21), FVector(-1500,400,9000)));
		TestTrue(TEXT("wing follows full translation and rotation"), Wing->GetActorTransform().Equals(ExpectedLocal * Ship->GetActorTransform(), .01));
		Graph->ContainerByObjectId.Add(100, 400);
		Graph->ApplyContainment(Ship, 100, 400); Graph->RefreshSpaceFrame();
		TestTrue(TEXT("stored ship and wing are hidden"), Ship->IsHidden() && Wing->IsHidden());
		TestFalse(TEXT("stored wing collision is disabled"), Wing->GetActorEnableCollision());
		Graph->ContainerByObjectId.Remove(100);
		Graph->ApplyContainment(Ship, 100, 0); Graph->RefreshSpaceFrame();
		TestFalse(TEXT("launched wing visibility returns"), Wing->IsHidden());
		TestTrue(TEXT("launched wing collision state returns"), Wing->GetActorEnableCollision());
		Graph->RemoveObject(100);
		TestTrue(TEXT("removing ship removes its generated wing"), Wing->IsActorBeingDestroyed());
		ASWGShip* LateShip = World->SpawnActor<ASWGShip>();
		if (!LateShip) { AddError(TEXT("late-owner fixture failed to spawn")); Cleanup(); return true; }
		Graph->RegisterStaticObject(500, LateShip);
		USWGPropAttachmentsComponent* LateAttachments = NewObject<USWGPropAttachmentsComponent>(LateShip);
		LateAttachments->Setup(*Props, Data, true); LateAttachments->RegisterComponent();
		TArray<AActor*> PendingChildren; LateShip->GetAttachedActors(PendingChildren);
		ASWGStaticProp* LateWing = nullptr;
		for (AActor* Child : PendingChildren)
			if (ASWGStaticProp* Prop = Cast<ASWGStaticProp>(Child)) { LateWing = Prop; break; }
		if (!TestNotNull(TEXT("late wing is attached before loading"), LateWing)) { Cleanup(); return true; }
		LateFuture = MakeShared<TFuture<FSWGMeshGenerationResult>>(Meshes->RequestMesh(LateWing, TEXT("appearance/mesh/hutt_fighter_heavy_struct_s01_l0.msh")));
		Meshes->Tick(1.f/60.f);
		TestFalse(TEXT("mesh completion is pending after worker dispatch"), LateFuture->IsReady());
		Graph->RemoveObject(500);
		TestTrue(TEXT("owner removal destroys its pending wing"), LateWing->IsActorBeingDestroyed());
		return false;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceMeshLifetimeTest, "SWG.Space.MeshWorkerLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

bool FSWGSpaceMeshLifetimeTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->AddToRoot(); Game->InitializeStandalone();
	UWorld* World = Game->GetWorldContext()->World();
	if (!World) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
	World->InitializeActorsForPlay(FURL());
	auto Cleanup = [Game, World]
	{
		Game->Shutdown(); GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false); World->RemoveFromRoot(); Game->RemoveFromRoot();
	};
	USWGMeshGeneratorSubsystem* Meshes = Game->GetSubsystem<USWGMeshGeneratorSubsystem>();
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	if (!Meshes || !Tre) { Cleanup(); return false; }
	Tre->LoadConfig();
	if (!TestTrue(TEXT("lifetime fixture loads real archives"), Tre->LoadArchives())) { Cleanup(); return false; }
	ASWGStaticProp* Actor = World->SpawnActor<ASWGStaticProp>();
	if (!Actor) { Cleanup(); return false; }
	const TWeakObjectPtr<USWGMeshGeneratorSubsystem> WeakMeshes(Meshes);
	const TWeakObjectPtr<USWGTreSubsystem> WeakTre(Tre);
	const TWeakObjectPtr<ASWGStaticProp> WeakActor(Actor);
	auto Future = MakeShared<TFuture<FSWGMeshGenerationResult>>(Meshes->RequestMesh(Actor, TEXT("appearance/mesh/hutt_fighter_heavy_struct_s01_l0.msh")));
	Meshes->Tick(1.f/60.f);
	TestFalse(TEXT("real worker is dispatched before owner shutdown"), Future->IsReady());
	Cleanup();
	CollectGarbage(RF_NoFlags, true);
	TestFalse(TEXT("mesh owner is collected before queued completion"), WeakMeshes.IsValid());
	TestFalse(TEXT("archive owner is collected before queued completion"), WeakTre.IsValid());
	TestFalse(TEXT("mesh actor is collected before queued completion"), WeakActor.IsValid());
	AddCommand(new FSWGAttachmentCommand([this, Future, Started = FPlatformTime::Seconds()]
	{
		if (!Future->IsReady())
		{
			if (FPlatformTime::Seconds()-Started < 60) return false;
			AddError(TEXT("collected-owner mesh completion timed out")); return true;
		}
		TestTrue(TEXT("completion after actual owner collection returns cancellation"), Future->Get().MeshOrComponent.IsType<FEmptyVariantState>());
		return true;
	}));
	return true;
}
#endif
