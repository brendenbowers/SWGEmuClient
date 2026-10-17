#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Components/SWGMovementComponent.h"
#include "Objects/Creature/SWGCreature.h"
#include "Engine/World.h"

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
	return true;
}
#endif
