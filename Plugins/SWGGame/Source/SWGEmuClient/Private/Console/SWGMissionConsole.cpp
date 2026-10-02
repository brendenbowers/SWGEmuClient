#include "Subsystems/SWGMissionSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWGMission, Log, All);

// swg.DumpMissions — logs the local player's mission_bag contents as USWGMissionSubsystem currently sees them.
static FAutoConsoleCommandWithWorldAndArgs GSWGDumpMissionsCommand(
	TEXT("swg.DumpMissions"),
	TEXT("Logs the current mission_bag entries (title, reward, difficulty)."),
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
		USWGMissionSubsystem* Missions = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr;
		if (!Missions)
		{
			UE_LOG(LogSWGMission, Warning, TEXT("swg.DumpMissions: no live mission subsystem — not in a session yet"));
			return;
		}

		const TArray<FSWGMissionEntry> Entries = Missions->GetMissions();
		UE_LOG(LogSWGMission, Log, TEXT("swg.DumpMissions: %d entr%s"), Entries.Num(), Entries.Num() == 1 ? TEXT("y") : TEXT("ies"));
		for (const FSWGMissionEntry& Entry : Entries)
		{
			UE_LOG(LogSWGMission, Log, TEXT("  %lld: populated=%d title='%s' desc='%s' target='%s' targetTemplate='%s' reward=%d difficulty=%d typeCrc=%08X dist=%.0fm dir=%s"),
				Entry.ObjectId, Entry.bPopulated, *Entry.Title.ToString(), *Entry.Description.ToString(),
				*Entry.TargetName, *Entry.TargetTemplateName.ToString(), Entry.RewardCredits, Entry.DifficultyDisplay, Entry.TypeCRC, Entry.DistanceMeters, *Entry.Direction);
		}
	}));

// swg.DumpAllMissions — every MISO ever seen a baseline/delta for, bypassing mission_bag
// containment and terminal-request scoping entirely. Use this to tell apart "the server
// never sent it" from "the client is filtering it out somewhere."
static FAutoConsoleCommandWithWorldAndArgs GSWGDumpAllMissionsCommand(
	TEXT("swg.DumpAllMissions"),
	TEXT("Logs every tracked MISO, unfiltered, including waypoint state."),
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
		USWGMissionSubsystem* Missions = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr;
		if (!Missions)
		{
			UE_LOG(LogSWGMission, Warning, TEXT("swg.DumpAllMissions: no live mission subsystem — not in a session yet"));
			return;
		}

		const TArray<FSWGMissionEntry> All = Missions->GetAllTrackedMissions();
		UE_LOG(LogSWGMission, Log, TEXT("swg.DumpAllMissions: %d tracked entr%s"), All.Num(), All.Num() == 1 ? TEXT("y") : TEXT("ies"));
		for (const FSWGMissionEntry& Entry : All)
		{
			UE_LOG(LogSWGMission, Log, TEXT("  %lld: populated=%d refresh=%u target='%s' hasWaypoint=%d wpActive=%d wpId=%lld wpName='%s' wpColor=%d wpRaw=(%.1f,%.1f,%.1f)"),
				Entry.ObjectId, Entry.bPopulated, Entry.RefreshCounter, *Entry.TargetName,
				Entry.bHasWaypoint, Entry.bWaypointActive, Entry.WaypointObjectId, *Entry.WaypointName, Entry.WaypointColor,
				Entry.WaypointRawPosition.X, Entry.WaypointRawPosition.Y, Entry.WaypointRawPosition.Z);
		}
	}));
