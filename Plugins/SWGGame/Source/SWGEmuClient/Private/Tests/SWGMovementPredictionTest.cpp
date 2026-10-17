#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Components/SWGMovementComponent.h"
#include "Objects/Creature/SWGCreature.h"
#include "Engine/World.h"
#include "Objects/World/SWGShip.h"
#include "Components/CapsuleComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGMovementPredictionTest, "SWGEmu.Movement.Prediction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGMovementPredictionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ASWGCreature* Actor = World->SpawnActor<ASWGCreature>();
	if (!TestNotNull(TEXT("test creature"), Actor)) return false;
	USWGMovementComponent* Movement = Actor->GetSWGMovementComponent();
	if (!TestNotNull(TEXT("movement component"), Movement)) return false;
	Movement->RunSpeed = 7.f;
	Movement->SetNetworkTarget(FVector::ZeroVector, 0.f);
	TestEqual(TEXT("first packet has no extrapolated velocity"), Movement->NetworkVelocity, FVector::ZeroVector);
	Movement->LastNetworkUpdateTime = World->GetTimeSeconds() - 0.1f;
	Movement->SetNetworkTarget(FVector(100, 0, 50), 0.f);
	TestTrue(TEXT("prediction velocity capped at run speed"), Movement->NetworkVelocity.Equals(FVector(700, 0, 0), 0.01));
	Actor->SetActorLocation(FVector::ZeroVector);
	for (int32 Frame = 0; Frame < 180 && Movement->NetworkTargetLocation.IsSet(); ++Frame) Movement->TickNetworkSmoothing(1.f / 60.f);
	TestTrue(TEXT("prediction stops after one update interval"), FMath::IsNearlyEqual(Actor->GetActorLocation().X, 310.0, 0.01));
	TestFalse(TEXT("network silence leaves no active smoothing target"), Movement->NetworkTargetLocation.IsSet());
	TestEqual(TEXT("prediction eventually idles"), Movement->Velocity, FVector::ZeroVector);
	Movement->LastNetworkUpdateTime = World->GetTimeSeconds() - 0.1f;
	Movement->SetNetworkTarget(FVector(100, 0, 50), 0.f);
	TestEqual(TEXT("repeated position is a stop report"), Movement->NetworkVelocity, FVector::ZeroVector);
	Movement->ClearNetworkTarget();
	Movement->SetNetworkTarget(FVector(10000, 0, 0), 0.f);
	TestEqual(TEXT("reset after teleport or containment clears prediction history"), Movement->NetworkVelocity, FVector::ZeroVector);
	Movement->LastNetworkUpdateTime = World->GetTimeSeconds() - 2.f;
	Movement->SetNetworkTarget(FVector(10100, 0, 0), 0.f);
	TestEqual(TEXT("old samples never extrapolate"), Movement->NetworkVelocity, FVector::ZeroVector);
	Movement->RunSpeed = 0.f;
	Movement->LastNetworkUpdateTime = World->GetTimeSeconds() - 0.1f;
	Movement->SetNetworkTarget(FVector(10200, 0, 0), 0.f);
	TestEqual(TEXT("unknown baseline speed uses interpolation only"), Movement->NetworkVelocity, FVector::ZeroVector);
	ASWGShip* FrameActor = World->SpawnActor<ASWGShip>();
	if (!TestNotNull(TEXT("ship frame"), FrameActor)) return false;
	Movement->ClearNetworkTarget();
	const FVector StandingPosition(50,20,88);
	const FQuat StandingRotation = FRotator(0,15,0).Quaternion();
	Actor->SetActorTransform(FTransform(StandingRotation, StandingPosition));
	Movement->SetNetworkTarget(StandingPosition, 15, FrameActor);
	Movement->TickNetworkSmoothing(.02f);
	TestTrue(TEXT("settled passenger retains a ship frame"), Movement->NetworkTargetLocation.IsSet());
	FrameActor->SetActorTransform(FTransform(FRotator(40,70,25), FVector(10000,2000,3000)));
	Movement->TickNetworkSmoothing(.02f);
	const FTransform StandingLocal = Actor->GetActorTransform().GetRelativeTransform(FrameActor->GetActorTransform());
	TestTrue(TEXT("idle passenger follows ship translation and roll"), StandingLocal.GetLocation().Equals(StandingPosition, .01));
	TestTrue(TEXT("idle passenger keeps orientation relative to floor"), StandingLocal.GetRotation().Equals(StandingRotation, .001));
	TestTrue(TEXT("ship motion does not produce a walking animation"), Movement->Velocity.IsNearlyZero());
	FrameActor->AddActorWorldOffset(FVector(50000,0,0));
	TestTrue(TEXT("ship translation cannot be mistaken for a passenger teleport"), Movement->GetNetworkTargetDistance2D(FrameActor->GetActorTransform().TransformPosition(StandingPosition), FrameActor) < .01);
	Movement->LastNetworkUpdateTime = World->GetTimeSeconds() - .1f;
	Movement->SetNetworkTarget(FrameActor->GetActorTransform().TransformPosition(StandingPosition), FrameActor->GetActorRotation().Yaw + 15, FrameActor);
	TestTrue(TEXT("same local report after ship motion has zero prediction velocity"), Movement->NetworkVelocity.IsNearlyZero(.01));
	Movement->TickNetworkSmoothing(.02f);
	Actor->GetCapsuleComponent()->SetCapsuleHalfHeight(Actor->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 12);
	TestTrue(TEXT("late appearance resizes along ship up"), Movement->AdjustNetworkCapsuleHeight(12));
	TestTrue(TEXT("late capsule resize preserves feet in tilted room"), Actor->GetActorTransform().GetRelativeTransform(FrameActor->GetActorTransform()).GetLocation().Equals(FVector(50,20,100), .01));
	TestTrue(TEXT("late capsule resize adjusts the pending local target"), Movement->NetworkTargetLocation->Equals(FVector(50,20,100), .01));
	const FVector WalkingPosition(150,20,100);
	Movement->RunSpeed = 7;
	Movement->SetNetworkTarget(FrameActor->GetActorTransform().TransformPosition(WalkingPosition), FrameActor->GetActorRotation().Yaw + 15, FrameActor);
	for (int32 Step = 0; Step < 180; ++Step)
	{
		FrameActor->SetActorTransform(FTransform(FRotator(30,70+Step,25), FVector(10000+Step*50,2000,3000)));
		Movement->TickNetworkSmoothing(1.f/60.f);
	}
	TestTrue(TEXT("passenger walks to local destination while ship moves"), Actor->GetActorTransform().GetRelativeTransform(FrameActor->GetActorTransform()).GetLocation().Equals(WalkingPosition, .01));
	const FVector BeforeRemoval = Actor->GetActorLocation();
	FrameActor->Destroy();
	Movement->TickNetworkSmoothing(.02f);
	TestFalse(TEXT("removed ship clears passenger frame target"), Movement->NetworkTargetLocation.IsSet());
	TestTrue(TEXT("removed ship cannot interpret local target as world position"), Actor->GetActorLocation().Equals(BeforeRemoval, .01));
	Movement->SetNetworkTarget(FVector(300,0,50), 0);
	TestTrue(TEXT("ordinary ground targets remain in world coordinates"), Movement->NetworkTargetLocation->Equals(FVector(300,0,50), .01));
	TestFalse(TEXT("ground capsule correction stays on existing path"), Movement->AdjustNetworkCapsuleHeight(12));
	return true;
}
#endif
