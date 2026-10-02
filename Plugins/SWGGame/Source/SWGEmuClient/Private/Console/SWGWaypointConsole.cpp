#include "Subsystems/SWGWaypointSubsystem.h"
#include "SWGLogCategories.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Components/SWGJournalComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"


// swg.DumpWaypoints — logs the local player's datapad waypoint list as USWGWaypointSubsystem currently sees it,
// and why it might be empty (no PLAY object found yet, or PLAY base8 hasn't arrived).
static FAutoConsoleCommandWithWorldAndArgs GSWGDumpWaypointsCommand(
	TEXT("swg.DumpWaypoints"),
	TEXT("Logs the current waypoint list (name, active, planet, distance, direction, world-loaded)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (GEngine && (!World || !World->IsGameWorld()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
		}
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGWaypointSubsystem* Waypoints = GameInstance ? GameInstance->GetSubsystem<USWGWaypointSubsystem>() : nullptr;
		if (!Waypoints)
		{
			UE_LOG(LogSWGWaypoint, Warning, TEXT("swg.DumpWaypoints: no live waypoint subsystem — not in a session yet"));
			return;
		}

		USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>();
		const USWGJournalComponent* Journal = ObjectGraph
			? ObjectGraph->FindComponent<USWGJournalComponent>(ObjectGraph->GetLocalPlayerObjectId())
			: nullptr;
		UE_LOG(LogSWGWaypoint, Log, TEXT("swg.DumpWaypoints: JournalComponent=%s bHasBase8=%s"),
			Journal ? TEXT("found") : TEXT("NOT FOUND (no PLAY object registered for the local player yet)"),
			Journal && Journal->bHasBase8 ? TEXT("true") : TEXT("false (no PLAY base8 baseline seen yet)"));

		const TArray<FSWGWaypointEntry> Entries = Waypoints->GetWaypoints();
		UE_LOG(LogSWGWaypoint, Log, TEXT("swg.DumpWaypoints: %d entr%s"), Entries.Num(), Entries.Num() == 1 ? TEXT("y") : TEXT("ies"));
		for (const FSWGWaypointEntry& Entry : Entries)
		{
			UE_LOG(LogSWGWaypoint, Log, TEXT("  %lld: '%s' active=%d planetCrc=%08X cell=%d dist=%.0fm dir=%s worldLoaded=%d raw=(%.1f,%.1f,%.1f)"),
				Entry.WaypointObjectId, *Entry.Name.ToString(), Entry.bActive, Entry.PlanetCRC, Entry.CellId,
				Entry.DistanceMeters, *Entry.Direction, Entry.bIsWorldLoaded,
				Entry.RawPosition.X, Entry.RawPosition.Y, Entry.RawPosition.Z);
		}
	}));
