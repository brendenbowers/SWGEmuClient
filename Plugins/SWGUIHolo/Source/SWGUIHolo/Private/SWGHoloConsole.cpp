#include "SWGHoloInventoryWidget.h"
#include "SWGPlacementViewMode.h"
#include "SWGPlacementMapMode.h"
#include "InputCoreTypes.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

namespace
{
	FAutoConsoleCommand CmdHoloInventorySelect(
		TEXT("swg.HoloInventory.Select"),
		TEXT("Pins the next ('swg.HoloInventory.Select 1') or previous (-1) worn item's details on an open holo inventory, as the D-pad does; 'swg.HoloInventory.Select bag 1' steps through the bag instead."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bBag = !Args.IsEmpty() && Args[0].Equals(TEXT("bag"), ESearchCase::IgnoreCase);
			const int32 DirectionArg = bBag ? 1 : 0;
			const int32 Direction = Args.IsValidIndex(DirectionArg) ? FCString::Atoi(*Args[DirectionArg]) : 1;
			for (TObjectIterator<USWGHoloInventoryWidget> It; It; ++It)
			{
				// Only one that is on screen; the class default object never is.
				if (It->GetCachedWidget().IsValid())
				{
					if (bBag)
					{
						It->SelectNextInBag(Direction);
					}
					else
					{
						It->SelectNext(Direction);
					}
				}
			}
		}));

	FAutoConsoleCommand CmdHoloInventoryPane(
		TEXT("swg.HoloInventory.Pane"),
		TEXT("Turns an open holo inventory's pane to the bag ('swg.HoloInventory.Pane inventory') or the character sheet ('swg.HoloInventory.Pane character')."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bCharacter = !Args.IsEmpty() && Args[0].StartsWith(TEXT("char"), ESearchCase::IgnoreCase);
			for (TObjectIterator<USWGHoloInventoryWidget> It; It; ++It)
			{
				if (It->GetCachedWidget().IsValid())
				{
					It->ShowPane(bCharacter ? ESWGHoloInventoryPane::Character : ESWGHoloInventoryPane::Inventory);
				}
			}
		}));

	// The placement widget lives in SWGUIWindow, so these go through the registry's delegates, not its class.
	FAutoConsoleCommand CmdPlaceMap(
		TEXT("swg.Place.Map"),
		TEXT("Toggles the holo map placement view during an active structure placement."),
		FConsoleCommandDelegate::CreateLambda([]() { FSWGPlacementViewRegistry::OnToggleRequested().Broadcast(TEXT("Map")); }));

	FAutoConsoleCommand CmdPlaceHolo(
		TEXT("swg.Place.Holo"),
		TEXT("Toggles the holo datapad placement view during an active structure placement."),
		FConsoleCommandDelegate::CreateLambda([]() { FSWGPlacementViewRegistry::OnToggleRequested().Broadcast(TEXT("Datapad")); }));

	FAutoConsoleCommand CmdPlaceFineTune(
		TEXT("swg.Place.FineTune"),
		TEXT("Enters the hologram preview from the locked holo map position."),
		FConsoleCommandDelegate::CreateLambda([]() { FSWGPlacementViewRegistry::OnCommandRequested().Broadcast(TEXT("Map"), TEXT("FineTune")); }));

	// Dev hooks for driving the mode without a mouse (PIE over MCP).
	FAutoConsoleCommand GSWGPlaceMapClickCommand(TEXT("swg.Place.Map.Click"),
		TEXT("swg.Place.Map.Click [rawX rawNorth] - locks a valid position."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			for (TObjectIterator<USWGPlacementMapMode> It; It; ++It)
			{
				if (!It->IsActive()) { continue; }
				if (Args.Num() >= 2) { It->SelectCandidate(FVector2D(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]))); }
				else { It->HandleMouseButtonDown(EKeys::LeftMouseButton); }
				break;
			}
		}));
	
	FAutoConsoleCommand GSWGPlaceMapKeyCommand(TEXT("swg.Place.Map.Key"),
		TEXT("swg.Place.Map.Key <KeyName> [shift] [ctrl] - sends a key to the placement map mode."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.IsEmpty()) { return; }
			const bool bShift = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("shift"), ESearchCase::IgnoreCase); });
			const bool bControl = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("ctrl"), ESearchCase::IgnoreCase); });
			for (TObjectIterator<USWGPlacementMapMode> It; It; ++It)
			{
				if (It->IsActive()) { UE_LOG(LogTemp, Log, TEXT("swg.Place.Map.Key %s: %s"), *Args[0], It->HandleKeyDown(FKey(*Args[0]), bShift, bControl) ? TEXT("handled") : TEXT("ignored")); break; }
			}
		}));

	FAutoConsoleCommand GSWGPlaceMapTurnCommand(TEXT("swg.Place.Map.Turn"),
		TEXT("swg.Place.Map.Turn <degrees> - turns the holo map to this yaw."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.IsEmpty()) { return; }
			for (TObjectIterator<USWGPlacementMapMode> It; It; ++It)
			{
				if (It->IsActive()) { It->HandleMouseButtonDown(EKeys::RightMouseButton); It->HandleMouseMove(FVector2D((FCString::Atof(*Args[0])) / 0.3f, 0.f)); It->HandleMouseButtonUp(EKeys::RightMouseButton); break; }
			}
		}));
	
	FAutoConsoleCommand GSWGPlaceMapTiltCommand(TEXT("swg.Place.Map.Tilt"),
		TEXT("swg.Place.Map.Tilt <dragPixels> - simulates a vertical right-drag on the holo map."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.IsEmpty()) { return; }
			for (TObjectIterator<USWGPlacementMapMode> It; It; ++It)
			{
				if (It->IsActive()) { It->HandleMouseButtonDown(EKeys::RightMouseButton); It->HandleMouseMove(FVector2D(0.f, FCString::Atof(*Args[0]))); It->HandleMouseButtonUp(EKeys::RightMouseButton); break; }
			}
		}));
}
