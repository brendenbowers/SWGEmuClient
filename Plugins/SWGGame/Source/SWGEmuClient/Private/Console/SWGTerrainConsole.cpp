#include "Subsystems/SWGTerrainSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	UWorld* FindGameWorld(UWorld* World)
	{
		if (GEngine && (!World || !World->IsGameWorld()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { return Context.World(); }
			}
		}
		return World;
	}

	USWGTerrainSubsystem* TerrainForConsole(UWorld* World)
	{
		World = FindGameWorld(World);
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
	}

	/** The local pawn's raw-space position, or false when there is none. */
	bool GetPlayerRawPosition(UWorld* World, FVector& OutRaw)
	{
		World = FindGameWorld(World);
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Pawn) { return false; }
		OutRaw = SWGToRawSpace(Pawn->GetActorLocation());
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTerrainTintCommand(
	TEXT("swg.Terrain.Tint"), TEXT("swg.Terrain.Tint [on|off] - retail ground tint (ACCN/ACRH/ACRF); re-bakes the loaded terrain tiles. Material needs the tint nodes (swg.AddTintToTerrainMaterial)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Terrain.TintEnabled"));
		if (!Enabled) { return; }
		const bool bOn = Args.IsEmpty() ? Enabled->GetInt() == 0 : Args[0].Equals(TEXT("on"), ESearchCase::IgnoreCase) || Args[0] == TEXT("1");
		Enabled->Set(bOn ? 1 : 0);
		if (USWGTerrainSubsystem* Terrain = TerrainForConsole(World)) { Terrain->RebakeAllTiles(); }
		UE_LOG(LogTemp, Log, TEXT("swg.Terrain.Tint: %s"), bOn ? TEXT("on") : TEXT("off"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTerrainGroundCommand(
	TEXT("swg.Terrain.Ground"), TEXT("swg.Terrain.Ground [rawX rawNorth] - ground tint and AEXC exclusion flag at a point (default: the player)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGTerrainSubsystem* Terrain = TerrainForConsole(World);
		FVector Raw = FVector::ZeroVector;
		if (Args.Num() >= 2) { Raw = FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), 0.0); }
		else if (!GetPlayerRawPosition(World, Raw)) { UE_LOG(LogTemp, Warning, TEXT("swg.Terrain.Ground: no player; pass rawX rawNorth")); return; }

		FSWGTerrainGroundSample Ground;
		if (!Terrain || !Terrain->GetGroundSampleAt(static_cast<float>(Raw.X), static_cast<float>(Raw.Y), Ground))
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Terrain.Ground: terrain not loaded"));
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("swg.Terrain.Ground (%.1f, %.1f): tint (%d,%d,%d) excluded=%d"),
			Raw.X, Raw.Y, Ground.Tint.R, Ground.Tint.G, Ground.Tint.B, Ground.bExcluded ? 1 : 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GWaterInfoCommand(
	TEXT("swg.Water.Info"), TEXT("swg.Water.Info [swimHeight=1] - water table, depth and swim/wade state under the player."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGTerrainSubsystem* Terrain = TerrainForConsole(World);
		FVector Raw;
		if (!Terrain || !GetPlayerRawPosition(World, Raw)) { UE_LOG(LogTemp, Warning, TEXT("swg.Water.Info: needs a player and a loaded terrain")); return; }

		const float SwimHeight = Args.IsEmpty() ? 1.0f : FCString::Atof(*Args[0]);
		const FSWGWaterInfo Info = Terrain->GetWaterInfoAt(static_cast<float>(Raw.X), static_cast<float>(Raw.Y), static_cast<float>(Raw.Z), SwimHeight);
		if (!Info.bHasWater)
		{
			UE_LOG(LogTemp, Log, TEXT("swg.Water.Info: no water at (%.1f, %.1f); ground %.2f"), Raw.X, Raw.Y, Info.GroundHeight);
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("swg.Water.Info: water %.2f ground %.2f point %.2f depth %.2f swimHeight %.2f | inWater=%d swimming=%d wading=%d serverWouldSwim=%d cameraUnderwater=%d"),
			Info.WaterHeight, Info.GroundHeight, Info.PointHeight, Info.Depth, Info.SwimHeight,
			Info.bPointInWater, Info.bSwimming, Info.bWading, Info.bServerWouldSwim, Terrain->IsCameraUnderwater());
	}));
