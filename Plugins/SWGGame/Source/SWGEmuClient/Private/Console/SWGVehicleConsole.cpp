#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Vehicle/SWGVehicleDynamicsComponent.h"
#include "Vehicle/SWGVehicleSubsystem.h"

#if !UE_BUILD_SHIPPING

namespace
{
	UWorld* FindVehicleGameWorld(UWorld* World)
	{
		if (World && World->IsGameWorld())
		{
			return World;
		}
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GVehicleInfoCommand(
	TEXT("swg.Vehicle.Info"),
	TEXT("swg.Vehicle.Info - lists every vehicle with hover dynamics: client data file, engine sounds and state, tilt."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UWorld* GameWorld = FindVehicleGameWorld(World);
		const USWGVehicleSubsystem* Vehicles = GameWorld ? GameWorld->GetSubsystem<USWGVehicleSubsystem>() : nullptr;
		if (!Vehicles)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Vehicle.Info: needs a running game"));
			return;
		}

		const TArray<USWGVehicleDynamicsComponent*> Dynamics = Vehicles->GetDynamics();
		UE_LOG(LogTemp, Warning, TEXT("swg.Vehicle.Info: %d vehicle(s) with hover dynamics"), Dynamics.Num());
		for (const USWGVehicleDynamicsComponent* Vehicle : Dynamics)
		{
			const FSWGVehicleEngineSounds& Sounds = Vehicle->GetEngineSounds();
			const FSWGHoverOutput& Output = Vehicle->GetLastOutput();
			UE_LOG(LogTemp, Warning, TEXT("  %s  data=%s  thrust levels=%d  engine=%s"), *GetNameSafe(Vehicle->GetOwner()),
				*Vehicle->GetClientDataPath(), Vehicle->GetThrustLevelCount(), Vehicle->GetEngineState() == ESWGEngineState::Running ? TEXT("running") : TEXT("idle"));
			UE_LOG(LogTemp, Warning, TEXT("    idle=%s accel=%s decel=%s run=%s"), *Sounds.Idle, *Sounds.Accel, *Sounds.Decel, *Sounds.Run);
			UE_LOG(LogTemp, Warning, TEXT("    pitch=%.2f deg roll=%.2f deg heightOffset=%.1f cm speed=%.0f%% turn=%.0f%% maxSpeed=%.0f"),
				FMath::RadiansToDegrees(Output.Pitch), FMath::RadiansToDegrees(Output.Roll), Output.HeightOffset,
				Output.SpeedFraction * 100.f, Output.TurnFraction * 100.f, Vehicle->GetHoverParams().MaxSpeed);
		}
	}));

#endif
