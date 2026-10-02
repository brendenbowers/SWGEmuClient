#include "SWGUISubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

// Console commands that open features and choose their presentation. They go through the router, so they work
// with whichever UI plugins are enabled.
namespace
{
	/** The first local player's router in the running game; the console's own world is the editor's under PIE. */
	USWGUISubsystem* GetUI()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.OwningGameInstance)
			{
				if (ULocalPlayer* LocalPlayer = Context.OwningGameInstance->GetFirstGamePlayer())
				{
					return LocalPlayer->GetSubsystem<USWGUISubsystem>();
				}
			}
		}
		return nullptr;
	}

	bool WantsHolo(const FString& Argument)
	{
		return Argument.StartsWith(TEXT("holo"), ESearchCase::IgnoreCase) || Argument.Equals(TEXT("on"), ESearchCase::IgnoreCase) || Argument == TEXT("1");
	}

	/** Features that open in more than one presentation, by the name the console uses. */
	const TPair<const TCHAR*, ESWGUIFeature> DualFeatures[] = {
		{ TEXT("inventory"), ESWGUIFeature::Inventory },
		{ TEXT("map"), ESWGUIFeature::PlanetMap },
		{ TEXT("survey"), ESWGUIFeature::Survey },
		{ TEXT("crafting"), ESWGUIFeature::Crafting },
	};

	FAutoConsoleCommand CmdToggleInventory(
		TEXT("swg.Inventory"),
		TEXT("Opens or closes the inventory, as the inventory key does. 'swg.Inventory dock' or 'window' forces that form regardless of the input device."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			USWGUISubsystem* UI = GetUI();
			if (!UI)
			{
				return;
			}
			if (Args.IsEmpty())
			{
				UI->ToggleInventory();
			}
			else
			{
				UI->CloseInventory();
				FSWGUIFeatureRequest Request;
				Request.bDocked = Args[0].Equals(TEXT("dock"), ESearchCase::IgnoreCase);
				UI->OpenFeature(ESWGUIFeature::Inventory, Request);
			}
		}));

	FAutoConsoleCommand CmdToggleWaypoints(
		TEXT("swg.Waypoints"),
		TEXT("Opens or closes the waypoint list, as the waypoint list key does."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (USWGUISubsystem* UI = GetUI())
			{
				UI->ToggleWaypointList();
			}
		}));

	FAutoConsoleCommand CmdTogglePlanetMap(
		TEXT("swg.Map"),
		TEXT("Opens or closes the planet map, as the planet map key does."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (USWGUISubsystem* UI = GetUI())
			{
				UI->TogglePlanetMap();
			}
		}));

	FAutoConsoleCommand CmdToggleDatapad(
		TEXT("swg.Datapad"),
		TEXT("Opens or closes the datapad, as the datapad key does."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (USWGUISubsystem* UI = GetUI())
			{
				UI->ToggleDatapad();
			}
		}));

	FAutoConsoleCommand CmdUIMode(
		TEXT("swg.UI.Mode"),
		TEXT("swg.UI.Mode <inventory|map|survey|crafting|all> <window|holo>: chooses where a feature opens. An open one swaps in place."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			USWGUISubsystem* UI = GetUI();
			if (!UI || Args.Num() < 2)
			{
				UE_LOG(LogTemp, Warning, TEXT("usage: swg.UI.Mode <inventory|map|survey|crafting|all> <window|holo>"));
				return;
			}
			const ESWGUIPresentation Presentation = WantsHolo(Args[1]) ? ESWGUIPresentation::Holo : ESWGUIPresentation::Window;
			for (const TPair<const TCHAR*, ESWGUIFeature>& Feature : DualFeatures)
			{
				if (Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase) || Args[0].Equals(Feature.Key, ESearchCase::IgnoreCase))
				{
					UI->SetPresentation(Feature.Value, Presentation);
				}
			}
		}));

	// The per-feature and all-at-once commands the holo work started with.
	FAutoConsoleCommand CmdPlanetMapMode(
		TEXT("swg.MapMode"), TEXT("Chooses the planet map's form: 'swg.MapMode window' or 'swg.MapMode holo'."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (USWGUISubsystem* UI = GetUI(); UI && !Args.IsEmpty()) { UI->SetPresentation(ESWGUIFeature::PlanetMap, WantsHolo(Args[0]) ? ESWGUIPresentation::Holo : ESWGUIPresentation::Window); }
		}));

	FAutoConsoleCommand CmdInventoryMode(
		TEXT("swg.InvMode"), TEXT("Chooses the inventory's form: 'swg.InvMode window' or 'swg.InvMode holo'."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (USWGUISubsystem* UI = GetUI(); UI && !Args.IsEmpty()) { UI->SetPresentation(ESWGUIFeature::Inventory, WantsHolo(Args[0]) ? ESWGUIPresentation::Holo : ESWGUIPresentation::Window); }
		}));

	FAutoConsoleCommand CmdCraftingMode(
		TEXT("swg.CraftingMode"), TEXT("Chooses the crafting form: 'swg.CraftingMode window' or 'swg.CraftingMode holo'."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (USWGUISubsystem* UI = GetUI(); UI && !Args.IsEmpty()) { UI->SetPresentation(ESWGUIFeature::Crafting, WantsHolo(Args[0]) ? ESWGUIPresentation::Holo : ESWGUIPresentation::Window); }
		}));

	FAutoConsoleCommand CmdSurveyMode(
		TEXT("swg.SurveyMode"), TEXT("Chooses the survey tool's form: 'swg.SurveyMode window' or 'swg.SurveyMode holo'."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (USWGUISubsystem* UI = GetUI(); UI && !Args.IsEmpty()) { UI->SetPresentation(ESWGUIFeature::Survey, WantsHolo(Args[0]) ? ESWGUIPresentation::Holo : ESWGUIPresentation::Window); }
		}));

	FAutoConsoleCommand CmdHoloMode(
		TEXT("swg.HoloMode"), TEXT("Sets every feature that has a holographic form at once: 'swg.HoloMode on' or 'swg.HoloMode off'."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			USWGUISubsystem* UI = GetUI();
			if (!UI || Args.IsEmpty())
			{
				return;
			}
			const ESWGUIPresentation Presentation = WantsHolo(Args[0]) ? ESWGUIPresentation::Holo : ESWGUIPresentation::Window;
			for (const TPair<const TCHAR*, ESWGUIFeature>& Feature : DualFeatures)
			{
				UI->SetPresentation(Feature.Value, Presentation);
			}
		}));
}
