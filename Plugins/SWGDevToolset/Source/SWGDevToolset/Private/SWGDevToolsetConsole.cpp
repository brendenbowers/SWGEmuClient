#include "DevToolset.h"
#include "SWGFxMaterialBuilder.h"
#include "SWGObjectScrollBuilder.h"
#include "SWGSkyMaterialBuilder.h"
#include "SWGTerrainTintBuilder.h"
#include "HAL/IConsoleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"

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

	FAutoConsoleCommand CreateFxMaterialsCommand(
		TEXT("swg.CreateFxMaterials"),
		TEXT("swg.CreateFxMaterials [rebuild] - generates the M_SWGParticle_* masters under /Game/SWGEmu/Materials; rebuild regenerates existing ones. Editor only, never during PIE."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			// Run once the asset registry has finished scanning; headless ExecCmds fire before that and stall.
			const bool bRebuild = Args.Contains(TEXT("rebuild"));
			const bool bQuitWhenDone = Args.Contains(TEXT("quit"));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bRebuild, bQuitWhenDone](float)
			{
				IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
				if (Registry.IsLoadingAssets())
				{
					return true;
				}
				SWGFxMaterials::BuildAll(bRebuild);
				if (bQuitWhenDone)
				{
					FPlatformMisc::RequestExit(false);
				}
				return false;
			}), 0.5f);
		}));

	FAutoConsoleCommand ObjectScrollCommand(
		TEXT("swg.AddScrollToObjectMaterials"),
		TEXT("swg.AddScrollToObjectMaterials [dump] [quit] - adds ScrollUV1/ScrollUV2 to the object masters (dump only logs their graphs). Editor only, never during PIE."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bDumpOnly = Args.Contains(TEXT("dump"));
			const bool bQuitWhenDone = Args.Contains(TEXT("quit"));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bDumpOnly, bQuitWhenDone](float)
			{
				IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
				if (Registry.IsLoadingAssets())
				{
					return true;
				}
				if (bDumpOnly)
				{
					SWGObjectScroll::Dump();
				}
				else
				{
					SWGObjectScroll::AddToObjectMaterials();
				}
				if (bQuitWhenDone)
				{
					FPlatformMisc::RequestExit(false);
				}
				return false;
			}), 0.5f);
		}));

	FAutoConsoleCommand TerrainTintCommand(
		TEXT("swg.AddTintToTerrainMaterial"),
		TEXT("swg.AddTintToTerrainMaterial [dump] [quit] - multiplies M_SWGTerrainBlend's base colour by the ground tint carried in the terrain mesh UV1/UV2 (dump only logs the graph). Editor only, never during PIE."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bDumpOnly = Args.Contains(TEXT("dump"));
			const bool bQuitWhenDone = Args.Contains(TEXT("quit"));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bDumpOnly, bQuitWhenDone](float)
			{
				IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
				if (Registry.IsLoadingAssets())
				{
					return true;
				}
				if (bDumpOnly)
				{
					SWGTerrainTintMaterial::Dump();
				}
				else
				{
					SWGTerrainTintMaterial::AddToTerrainMaterial();
				}
				if (bQuitWhenDone)
				{
					FPlatformMisc::RequestExit(false);
				}
				return false;
			}), 0.5f);
		}));
}
