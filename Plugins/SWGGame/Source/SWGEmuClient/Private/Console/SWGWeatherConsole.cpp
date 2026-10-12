#include "Subsystems/SWGWeatherSubsystem.h"

#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

#if !UE_BUILD_SHIPPING

namespace
{
	// Resolved at invocation, not captured: a console command outlives the subsystem.
	USWGWeatherSubsystem* FindLiveWeatherSubsystem()
	{
		for (TObjectIterator<USWGWeatherSubsystem> It; It; ++It)
		{
			if (IsValid(*It) && It->GetGameInstance())
			{
				return *It;
			}
		}
		return nullptr;
	}
}

static FAutoConsoleCommand SWGWeatherCmd(
	TEXT("swg.Weather"),
	TEXT("swg.Weather <0-4> [windX windY] - sets the weather as the server would (0 clear .. 4 extreme storm; wind east and north, -1..1). No arguments prints the state."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			USWGWeatherSubsystem* Weather = FindLiveWeatherSubsystem();
			if (!Weather)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.Weather: no live weather subsystem"));
				return;
			}
			if (Args.IsEmpty())
			{
				for (const FString& Line : Weather->DescribeState())
				{
					UE_LOG(LogTemp, Display, TEXT("weather: %s"), *Line);
				}
				return;
			}

			FVector Wind = Weather->GetWindVector();
			if (Args.Num() >= 3)
			{
				Wind = FVector(FCString::Atof(*Args[1]), 0.0, FCString::Atof(*Args[2]));
			}
			else if (Wind.IsNearlyZero())
			{
				// Without a wind the storm would have nothing to blow, so test with a steady north-east one.
				Wind = FVector(0.7, 0.0, 0.7);
			}
			Weather->SetWeather(FCString::Atoi(*Args[0]), Wind);
			UE_LOG(LogTemp, Display, TEXT("swg.Weather: weather %d, wind east %.2f north %.2f"), Weather->GetWeatherIndex(), Wind.X, Wind.Z);
		}));

static FAutoConsoleCommand SWGWeatherInfoCmd(
	TEXT("swg.Weather.Info"),
	TEXT("swg.Weather.Info - prints the weather index, environment row, camera particle, fog and wind."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
		{
			const USWGWeatherSubsystem* Weather = FindLiveWeatherSubsystem();
			if (!Weather)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.Weather.Info: no live weather subsystem"));
				return;
			}
			for (const FString& Line : Weather->DescribeState())
			{
				UE_LOG(LogTemp, Display, TEXT("weather: %s"), *Line);
			}
		}));

#endif
