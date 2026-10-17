#include "Subsystems/SWGAnimatedMaterialSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

static FAutoConsoleCommandWithWorldAndArgs GShaderAnimCommand(
	TEXT("swg.Fx.ShaderAnim"),
	TEXT("swg.Fx.ShaderAnim - lists the animated object materials (SWTS/SWSH flipbooks, TSNS scrolls) and their current state. swg.Fx.ShaderAnimation 0 disables them."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (!GameInstance && GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.OwningGameInstance)
				{
					GameInstance = Context.OwningGameInstance;
				}
			}
		}
		const USWGAnimatedMaterialSubsystem* Animated = GameInstance ? GameInstance->GetSubsystem<USWGAnimatedMaterialSubsystem>() : nullptr;
		if (!Animated)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Fx.ShaderAnim: needs a running game"));
			return;
		}
		const TArray<FString> Lines = Animated->DescribeEntries();
		UE_LOG(LogTemp, Warning, TEXT("swg.Fx.ShaderAnim: %d animated material(s)"), Lines.Num());
		for (const FString& Line : Lines)
		{
			UE_LOG(LogTemp, Warning, TEXT("  %s"), *Line);
		}
	}));

#endif // !UE_BUILD_SHIPPING
