#include "Subsystems/SWGStructurePlacementSubsystem.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Network/Messages/Zone/ObjectMenuSelectMessage.h"
#include "Objects/World/SWGStructurePlacementPreview.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWGRadial, Log, All);

namespace
{
	USWGStructurePlacementSubsystem* PlacementForConsole(UWorld* World)
	{
		if (GEngine && (!World || !World->IsGameWorld()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
		}
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGStructurePlacementSubsystem>() : nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GPlaceFakeCommand(
	TEXT("swg.Place.Fake"), TEXT("swg.Place.Fake <templatePath> — open placement without a deed."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGStructurePlacementSubsystem* Placement = PlacementForConsole(World);
		if (!Placement || Args.IsEmpty() || !Placement->BeginFake(Args[0]))
		{
			UE_LOG(LogTemp, Warning, TEXT("usage: swg.Place.Fake <object template path> (requires PIE)"));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GPlaceCheckCommand(
	TEXT("swg.Place.Check"), TEXT("swg.Place.Check <rawX> <rawNorth> <angle 0..3>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGStructurePlacementSubsystem* Placement = PlacementForConsole(World);
		if (!Placement || !Placement->IsActive() || Args.Num() != 3)
		{
			UE_LOG(LogTemp, Warning, TEXT("usage: swg.Place.Check <rawX> <rawNorth> <angle 0..3> (requires active placement)"));
			return;
		}
		const int32 Angle = FCString::Atoi(*Args[2]);
		Placement->Rotate(Angle - Placement->GetRotation());
		Placement->SetCandidate(FVector2D(FCString::Atof(*Args[0]), FCString::Atof(*Args[1])));
		const FSWGPlacementValidation Result = Placement->GetValidation();
		UE_LOG(LogTemp, Log, TEXT("swg.Place.Check: %s %s %s"),
			Result.Verdict == ESWGPlacementVerdict::Invalid ? TEXT("invalid") : Result.Verdict == ESWGPlacementVerdict::Uncertain ? TEXT("uncertain") : TEXT("valid"),
			*Result.ReasonStringId, *Result.ReasonParam);
	}));

static FAutoConsoleCommandWithWorldAndArgs GPlacePrintCommand(
	TEXT("swg.Place.Print"), TEXT("swg.Place.Print <progress 0..1> — inspect the construction reveal in PIE."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGStructurePlacementSubsystem* Placement = PlacementForConsole(World);
		ASWGStructurePlacementPreview* Preview = Placement ? Placement->GetPreview() : nullptr;
		if (!Preview || Args.IsEmpty()) { return; }
		Preview->SetPresentationHidden(false);
		Preview->BeginConstructionPrint();
		Preview->SetConstructionProgress(FCString::Atof(*Args[0]));
	}));

static FAutoConsoleCommandWithWorldAndArgs GSWGPlaceModeCommand(
	TEXT("swg.Place.Mode"), TEXT("swg.Place.Mode <deedId> [use] — request structure placement mode directly or through deed Use."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (GEngine && (!World || !World->IsGameWorld()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
		}
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGCommandSubsystem* Commands = GameInstance ? GameInstance->GetSubsystem<USWGCommandSubsystem>() : nullptr;
		USWGNetworkSubsystem* Network = GameInstance ? GameInstance->GetSubsystem<USWGNetworkSubsystem>() : nullptr;
		const int64 DeedId = Args.Num() >= 1 ? FCString::Atoi64(*Args[0]) : 0;
		const bool bUseRadial = Args.Num() == 2 && Args[1].Equals(TEXT("use"), ESearchCase::IgnoreCase);
		if (DeedId <= 0 || (Args.Num() != 1 && !bUseRadial) || (bUseRadial ? !Network : !Commands))
		{
			UE_LOG(LogSWGRadial, Warning, TEXT("usage: swg.Place.Mode <deedId> [use]"));
			return;
		}
		if (bUseRadial)
		{
			FObjectMenuSelectMessage Select;
			Select.ObjectId = DeedId;
			Select.RadialId = 20;
			Network->SendMessage(Select.Serialize());
			UE_LOG(LogSWGRadial, Log, TEXT("sent deed Use selection for %lld"), DeedId);
		}
		else { Commands->SendCommand(TEXT("placestructuremode"), DeedId); }
	}));
