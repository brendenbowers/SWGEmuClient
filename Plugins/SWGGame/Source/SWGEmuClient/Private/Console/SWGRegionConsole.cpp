#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/SWGRegionSubsystem.h"

#if !UE_BUILD_SHIPPING

namespace
{
	USWGRegionSubsystem* FindRegionSubsystem(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (!GameInstance && GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.OwningGameInstance)
				{
					GameInstance = Context.OwningGameInstance;
					break;
				}
			}
		}
		return GameInstance ? GameInstance->GetSubsystem<USWGRegionSubsystem>() : nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GRegionInfoCommand(
	TEXT("swg.Region.Info"),
	TEXT("swg.Region.Info [list] - the planet's client regions and the one the player is in; 'list' prints every row."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGRegionSubsystem* Regions = FindRegionSubsystem(World);
		if (!Regions)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Region.Info: needs a running game"));
			return;
		}
		Regions->EnsureLoaded();
		UE_LOG(LogTemp, Warning, TEXT("swg.Region.Info: planet '%s', %d region(s), player in '%s'"),
			*Regions->GetPlanet(), Regions->GetRegions().Regions.Num(), *Regions->GetCurrentRegionName());
		if (!Args.IsEmpty() && Args[0].Equals(TEXT("list"), ESearchCase::IgnoreCase))
		{
			for (const FSWGClientRegion& Region : Regions->GetRegions().Regions)
			{
				UE_LOG(LogTemp, Warning, TEXT("  %s  x=%.0f z=%.0f radius=%.0f"), *Region.NameReference, Region.Center.X, Region.Center.Y, Region.Radius);
			}
		}
	}));

#endif
