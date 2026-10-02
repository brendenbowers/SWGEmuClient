// Copyright Epic Games, Inc. All Rights Reserved.

#include "DevToolset.h"
#include "Common/SWGWorldScale.h"
#include "Algo/Reverse.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/StringOutputDevice.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Containers/Ticker.h"

#include <queue>

#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif

namespace
{
	constexpr float RoutePointRadius = 200.f;
	constexpr float DestinationPadding = 150.f;
	constexpr float RepathInterval = 1.f;
	constexpr float GridStep = 300.f;
	constexpr float MaxStepHeight = 200.f;
	constexpr int32 GridHalfExtent = 32;
	constexpr int32 GridSide = GridHalfExtent * 2 + 1;

	struct FDevNavigationState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<APawn> Pawn;
		TWeakObjectPtr<AActor> Target;
		TArray<FVector> PathPoints;
		FVector TargetLocationAtRepath = FVector::ZeroVector;
		int32 PointIndex = 1;
		double NextRepathTime = 0.0;
		bool bPartialPath = false;
	};

	FDevNavigationState NavigationState;
	FTSTicker::FDelegateHandle NavigationTickHandle;

	int32 AdvancePastReachedPoints(const TArray<FVector>& Points, int32 Index, const FVector& Location, float Radius)
	{
		while (Index < Points.Num() - 1 && FVector::DistSquared2D(Location, Points[Index]) <= FMath::Square(Radius))
		{
			++Index;
		}
		return Index;
	}

	bool HasReachedPathEnd(const TArray<FVector>& Points, const FVector& Location, float Radius)
	{
		return !Points.IsEmpty() && FVector::DistSquared2D(Location, Points.Last()) <= FMath::Square(Radius);
	}

	AActor* FindActorByName(UWorld& World, const FString& Name)
	{
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			if (It->GetName().Equals(Name, ESearchCase::IgnoreCase)
				|| It->GetActorLabel().Equals(Name, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
		return nullptr;
	}

	AActor* ResolveTarget(UWorld& PlayWorld, const FString& EntityName)
	{
		if (!EntityName.IsEmpty())
		{
			return FindActorByName(PlayWorld, EntityName);
		}

		if (!GEditor)
		{
			return nullptr;
		}
		for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
		{
			if (AActor* Selected = Cast<AActor>(*It))
			{
				return Selected->GetWorld() == &PlayWorld ? Selected : FindActorByName(PlayWorld, Selected->GetName());
			}
		}
		return nullptr;
	}

	FVector GetDestination(const AActor& Target, const FVector& PawnLocation)
	{
		FVector BoundsOrigin;
		FVector BoundsExtent;
		Target.GetActorBounds(true, BoundsOrigin, BoundsExtent);
		const float BoundsRadius = FMath::Clamp(FVector2D(BoundsExtent.X, BoundsExtent.Y).Size(), 0.f, 2000.f);
		const float AcceptanceRadius = BoundsRadius + DestinationPadding;

		FVector FromTarget = PawnLocation - BoundsOrigin;
		FromTarget.Z = 0.f;
		return BoundsOrigin + FromTarget.GetSafeNormal() * AcceptanceRadius;
	}

	using FGroundSampler = TFunctionRef<bool(const FVector2D&, FVector&)>;
	using FEdgeBlocker = TFunctionRef<bool(const FVector&, const FVector&)>;

	bool BuildTerrainGridPath(const FVector& Start, const FVector& Goal, FGroundSampler SampleGround,
		FEdgeBlocker IsBlocked, TArray<FVector>& OutPath, bool& bOutPartial)
	{
		const FVector2D FullDelta(Goal.X - Start.X, Goal.Y - Start.Y);
		FVector2D SearchDelta = FullDelta;
		const float SearchRadius = GridHalfExtent * GridStep;
		if (SearchDelta.SizeSquared() > FMath::Square(SearchRadius))
		{
			SearchDelta = SearchDelta.GetSafeNormal() * SearchRadius;
		}
		const FIntPoint GoalCell(
			FMath::Clamp(FMath::RoundToInt(SearchDelta.X / GridStep), -GridHalfExtent, GridHalfExtent),
			FMath::Clamp(FMath::RoundToInt(SearchDelta.Y / GridStep), -GridHalfExtent, GridHalfExtent));
		bOutPartial = !SearchDelta.Equals(FullDelta, GridStep);

		auto ToIndex = [](const FIntPoint Cell)
		{
			return (Cell.Y + GridHalfExtent) * GridSide + Cell.X + GridHalfExtent;
		};
		auto ToCell = [](const int32 Index)
		{
			return FIntPoint(Index % GridSide - GridHalfExtent, Index / GridSide - GridHalfExtent);
		};

		const int32 NodeCount = GridSide * GridSide;
		TArray<float> Costs;
		Costs.Init(TNumericLimits<float>::Max(), NodeCount);
		TArray<int32> Parents;
		Parents.Init(INDEX_NONE, NodeCount);
		TArray<FVector> GroundPoints;
		GroundPoints.SetNumUninitialized(NodeCount);
		TArray<uint8> GroundStates;
		GroundStates.Init(0, NodeCount);
		TBitArray<> Closed(false, NodeCount);

		auto GetGround = [&](const int32 Index, FVector& OutGround)
		{
			if (GroundStates[Index] == 0)
			{
				const FIntPoint Cell = ToCell(Index);
				const FVector2D XY(Start.X + Cell.X * GridStep, Start.Y + Cell.Y * GridStep);
				GroundStates[Index] = SampleGround(XY, GroundPoints[Index]) ? 1 : 2;
			}
			OutGround = GroundPoints[Index];
			return GroundStates[Index] == 1;
		};

		struct FOpenNode { int32 Index; float Score; };
		struct FLowerScore { bool operator()(const FOpenNode& A, const FOpenNode& B) const { return A.Score > B.Score; } };
		std::priority_queue<FOpenNode, std::vector<FOpenNode>, FLowerScore> Open;
		const int32 StartIndex = ToIndex(FIntPoint::ZeroValue);
		const int32 GoalIndex = ToIndex(GoalCell);
		int32 ClosestIndex = StartIndex;
		float ClosestDistanceSquared = SearchDelta.SizeSquared();
		Costs[StartIndex] = 0.f;
		Open.push({ StartIndex, static_cast<float>(SearchDelta.Size()) });

		static const FIntPoint Neighbors[] = {
			{ -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
			{ 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 }
		};
		while (!Open.empty())
		{
			const int32 CurrentIndex = Open.top().Index;
			Open.pop();
			if (Closed[CurrentIndex])
			{
				continue;
			}
			Closed[CurrentIndex] = true;

			FVector CurrentGround;
			if (!GetGround(CurrentIndex, CurrentGround))
			{
				continue;
			}
			const FIntPoint CurrentCell = ToCell(CurrentIndex);
			const float DistanceSquared = FVector2D::DistSquared(
				FVector2D(CurrentCell.X * GridStep, CurrentCell.Y * GridStep), SearchDelta);
			if (DistanceSquared < ClosestDistanceSquared)
			{
				ClosestDistanceSquared = DistanceSquared;
				ClosestIndex = CurrentIndex;
			}
			if (CurrentIndex == GoalIndex)
			{
				break;
			}
			for (const FIntPoint Offset : Neighbors)
			{
				const FIntPoint NextCell = CurrentCell + Offset;
				if (FMath::Abs(NextCell.X) > GridHalfExtent || FMath::Abs(NextCell.Y) > GridHalfExtent)
				{
					continue;
				}
				const int32 NextIndex = ToIndex(NextCell);
				FVector NextGround;
				if (Closed[NextIndex] || !GetGround(NextIndex, NextGround)
					|| FMath::Abs(NextGround.Z - CurrentGround.Z) > MaxStepHeight
					|| IsBlocked(CurrentGround, NextGround))
				{
					continue;
				}

				const float NewCost = Costs[CurrentIndex] + FVector::Distance(CurrentGround, NextGround);
				if (NewCost >= Costs[NextIndex])
				{
					continue;
				}
				Costs[NextIndex] = NewCost;
				Parents[NextIndex] = CurrentIndex;
				const FVector2D Remaining((GoalCell.X - NextCell.X) * GridStep, (GoalCell.Y - NextCell.Y) * GridStep);
				Open.push({ NextIndex, NewCost + static_cast<float>(Remaining.Size()) });
			}
		}

		const int32 EndIndex = Closed[GoalIndex] ? GoalIndex : ClosestIndex;
		bOutPartial |= EndIndex != GoalIndex;
		if (EndIndex == StartIndex)
		{
			return false;
		}
		OutPath.Reset();
		for (int32 Index = EndIndex; Index != INDEX_NONE; Index = Parents[Index])
		{
			FVector Ground;
			if (!GetGround(Index, Ground))
			{
				return false;
			}
			OutPath.Add(Ground);
		}
		Algo::Reverse(OutPath);
		return OutPath.Num() > 1;
	}

	bool RebuildPath()
	{
		UWorld* World = NavigationState.World.Get();
		APawn* Pawn = NavigationState.Pawn.Get();
		AActor* Target = NavigationState.Target.Get();
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGTerrainSubsystem* Terrain = GameInstance ? GameInstance->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
		if (!Terrain || !Pawn || !Target)
		{
			return false;
		}

		const FVector Destination = GetDestination(*Target, Pawn->GetActorLocation());
		const auto SampleGround = [Terrain](const FVector2D& XY, FVector& Ground)
		{
			const FVector Raw = SWGToRawSpace(FVector(XY, 0.f));
			const float Height = Terrain->GetHeightAt(Raw.X, Raw.Y);
			Ground = FVector(XY, SWGToUnrealSpace(Height));
			return FMath::IsFinite(Height);
		};
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SWGDevTerrainPath), false, Pawn);
		const auto IsBlocked = [World, &TraceParams](const FVector& A, const FVector& B)
		{
			return World->LineTraceTestByChannel(A + FVector(0, 0, 100.f), B + FVector(0, 0, 100.f), ECC_Visibility, TraceParams);
		};
		if (!BuildTerrainGridPath(Pawn->GetActorLocation(), Destination, SampleGround, IsBlocked,
			NavigationState.PathPoints, NavigationState.bPartialPath))
		{
			NavigationState.PathPoints.Reset();
			return false;
		}

		NavigationState.PointIndex = 1;
		NavigationState.TargetLocationAtRepath = Target->GetActorLocation();
		return true;
	}

	bool TickNavigation(float)
	{
		UWorld* World = NavigationState.World.Get();
		APawn* Pawn = NavigationState.Pawn.Get();
		AActor* Target = NavigationState.Target.Get();
		if (!World || !Pawn || !Target)
		{
			NavigationState = {};
			NavigationTickHandle.Reset();
			return false;
		}

		const FVector Destination = GetDestination(*Target, Pawn->GetActorLocation());
		if (FVector::DistSquared2D(Pawn->GetActorLocation(), Destination) <= FMath::Square(RoutePointRadius))
		{
			NavigationState = {};
			NavigationTickHandle.Reset();
			return false;
		}

		const double Now = World->GetTimeSeconds();
		if (Now >= NavigationState.NextRepathTime
			&& (NavigationState.PathPoints.IsEmpty()
				|| HasReachedPathEnd(NavigationState.PathPoints, Pawn->GetActorLocation(), RoutePointRadius)
				|| FVector::DistSquared2D(Target->GetActorLocation(), NavigationState.TargetLocationAtRepath) > FMath::Square(GridStep)))
		{
			RebuildPath();
			NavigationState.NextRepathTime = Now + RepathInterval;
		}

		for (int32 Index = 1; Index < NavigationState.PathPoints.Num(); ++Index)
		{
			DrawDebugLine(World, NavigationState.PathPoints[Index - 1], NavigationState.PathPoints[Index], FColor::Cyan, false, 0.f, 0, 5.f);
		}
		DrawDebugSphere(World, Destination, 40.f, 12, FColor::Green, false, 0.f, 0, 4.f);

		NavigationState.PointIndex = AdvancePastReachedPoints(
			NavigationState.PathPoints, NavigationState.PointIndex, Pawn->GetActorLocation(), RoutePointRadius);
		if (NavigationState.PathPoints.IsValidIndex(NavigationState.PointIndex))
		{
			FVector Direction = NavigationState.PathPoints[NavigationState.PointIndex] - Pawn->GetActorLocation();
			Direction.Z = 0.f;
			Pawn->AddMovementInput(Direction.GetSafeNormal());
		}
		return true;
	}
}

FString UDevToolset::ExecuteConsoleCommand(const FString& Command)
{
	if (!GEngine)
	{
		return FString();
	}

	FStringOutputDevice OutputDevice;
	OutputDevice.SetAutoEmitLineTerminator(true);

	// Prefer an active PIE world so gameplay-only commands work; fall back to any
	// editor world context (there's no active PIE session outside of Play).
	UWorld* World = GEngine->GetCurrentPlayWorld();
	if (!World)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				World = Context.World();
				break;
			}
		}
	}

	GEngine->Exec(World, *Command, OutputDevice);

	return OutputDevice;
}

bool UDevToolset::RecompileLiveCoding()
{
#if PLATFORM_WINDOWS
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>("LiveCoding");
	if (!LiveCoding || !LiveCoding->IsEnabledForSession())
	{
		return false;
	}

	ELiveCodingCompileResult Result = ELiveCodingCompileResult::Failure;
	LiveCoding->Compile(ELiveCodingCompileFlags::WaitForCompletion, &Result);

	return Result == ELiveCodingCompileResult::Success || Result == ELiveCodingCompileResult::NoChanges;
#else
	return false;
#endif
}

FString UDevToolset::NavigateToEntity(const FString& EntityName)
{
	StopNavigation();

	UWorld* World = GEngine ? GEngine->GetCurrentPlayWorld() : nullptr;
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!World || !Pawn)
	{
		return TEXT("Start PIE and possess the player before navigating.");
	}

	AActor* Target = ResolveTarget(*World, EntityName);
	if (!Target)
	{
		return EntityName.IsEmpty()
			? TEXT("No actor is selected.")
			: FString::Printf(TEXT("No PIE actor named '%s' was found."), *EntityName);
	}

	NavigationState.World = World;
	NavigationState.Pawn = Pawn;
	NavigationState.Target = Target;
	NavigationState.NextRepathTime = World->GetTimeSeconds() + RepathInterval;
	const bool bHasPath = RebuildPath();
	NavigationTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickNavigation));

	return FString::Printf(TEXT("Navigating to %s: %s (%d path points%s)."), *Target->GetName(),
		bHasPath ? TEXT("terrain route ready") : TEXT("no terrain route found yet"), NavigationState.PathPoints.Num(),
		NavigationState.bPartialPath ? TEXT(", partial") : TEXT(""));
}

bool UDevToolset::StopNavigation()
{
	const bool bWasActive = NavigationTickHandle.IsValid();
	if (bWasActive)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(NavigationTickHandle);
	}
	NavigationTickHandle.Reset();
	NavigationState = {};
	return bWasActive;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGDevNavigationPointAdvanceTest,
	"SWG.DevToolset.Navigation.PointAdvance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGDevNavigationPointAdvanceTest::RunTest(const FString& Parameters)
{
	const TArray<FVector> Points{ FVector::ZeroVector, FVector(50, 0, 0), FVector(500, 0, 0) };
	TestEqual(TEXT("skips reached intermediate points"), AdvancePastReachedPoints(Points, 1, FVector::ZeroVector, 100.f), 2);
	TestEqual(TEXT("keeps an unreached point"), AdvancePastReachedPoints(Points, 1, FVector(-100, 0, 0), 100.f), 1);
	TestFalse(TEXT("keeps the route while its end is distant"), HasReachedPathEnd(Points, FVector::ZeroVector, 100.f));
	TestTrue(TEXT("replans after reaching the route end"), HasReachedPathEnd(Points, FVector(450, 0, 0), 100.f));

	TArray<FVector> TerrainPath;
	bool bPartial = false;
	const auto FlatGround = [](const FVector2D& XY, FVector& Ground)
	{
		Ground = FVector(XY, 0.f);
		return true;
	};
	const auto WallWithGap = [](const FVector& A, const FVector& B)
	{
		const FVector Midpoint = (A + B) * 0.5f;
		return Midpoint.X > 400.f && Midpoint.X < 800.f && FMath::Abs(Midpoint.Y) < 500.f;
	};
	TestTrue(TEXT("finds a terrain route around an obstacle"), BuildTerrainGridPath(
		FVector::ZeroVector, FVector(1200, 0, 0), FlatGround, WallWithGap, TerrainPath, bPartial));
	TestTrue(TEXT("route uses the wall gap"), TerrainPath.ContainsByPredicate([](const FVector& Point)
	{
		return FMath::Abs(Point.Y) >= 600.f;
	}));
	const auto BlockGoal = [](const FVector&, const FVector& B)
	{
		return FVector::DistSquared2D(B, FVector(1200, 0, 0)) <= FMath::Square(300.f);
	};
	TestTrue(TEXT("returns a partial route when the destination is blocked"), BuildTerrainGridPath(
		FVector::ZeroVector, FVector(1200, 0, 0), FlatGround, BlockGoal, TerrainPath, bPartial));
	TestTrue(TEXT("blocked destination marks the route partial"), bPartial);
	TestTrue(TEXT("partial route advances toward the destination"),
		FVector::DistSquared2D(TerrainPath.Last(), FVector(1200, 0, 0)) < FMath::Square(1200.f));
	return true;
}
#endif
