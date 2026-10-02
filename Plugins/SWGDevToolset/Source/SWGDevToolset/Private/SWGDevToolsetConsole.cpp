#include "DevToolset.h"
#include "SWGSkyMaterialBuilder.h"
#include "HAL/IConsoleManager.h"

namespace
{
	FAutoConsoleCommand NavigateToEntityCommand(
		TEXT("swg.NavigateTo"),
		TEXT("swg.NavigateTo [actor name] - follows a path to that PIE actor; omit the name to use the selected actor."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			UE_LOG(LogTemp, Display, TEXT("%s"), *UDevToolset::NavigateToEntity(FString::Join(Args, TEXT(" "))));
		}));

	FAutoConsoleCommand StopNavigationCommand(
		TEXT("swg.StopNavigation"),
		TEXT("Stops movement started by swg.NavigateTo."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UE_LOG(LogTemp, Display, TEXT("Navigation %s."), UDevToolset::StopNavigation() ? TEXT("stopped") : TEXT("was not active"));
		}));

	FAutoConsoleCommand CreateSkyMaterialsCommand(
		TEXT("swg.CreateSkyMaterials"),
		TEXT("Generates M_SWGSkySpriteAlpha, M_SWGCloudLayer and M_SWGStarField under /Game/SWGEmu/Materials (skips ones that exist)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			SWGSkyMaterials::BuildAll();
		}));
}
