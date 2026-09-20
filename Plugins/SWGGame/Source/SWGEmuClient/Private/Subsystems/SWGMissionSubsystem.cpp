#include "Subsystems/SWGMissionSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/Zone/UpdateContainmentMessage.h"
#include "Network/Objects/Zone/Mission/MissionObjectBaseline.h"
#include "Network/Objects/Zone/Mission/MissionObjectDelta.h"
#include "Network/Messages/Zone/Object/MissionAccept.h"
#include "Network/Messages/Zone/Object/MissionListRequest.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Common/SWGWorldScale.h"

namespace
{
	/** 8-point compass bearing from PlayerRaw to TargetRaw, both in raw (x east, y north) space. Empty/zero if they're on top of each other. */
	FString CompassDirection(const FVector& PlayerRaw, const FVector& TargetRaw, float& OutBearingDegrees)
	{
		const float East = TargetRaw.X - PlayerRaw.X;
		const float North = TargetRaw.Y - PlayerRaw.Y;
		if (FMath::IsNearlyZero(East) && FMath::IsNearlyZero(North))
		{
			OutBearingDegrees = 0.f;
			return FString();
		}

		// 0 at north, clockwise, wrapped to [0, 360).
		float Bearing = FMath::RadiansToDegrees(FMath::Atan2(East, North));
		if (Bearing < 0.f) { Bearing += 360.f; }
		OutBearingDegrees = Bearing;

		static const TCHAR* Points[] = { TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW") };
		const int32 Index = FMath::RoundToInt(Bearing / 45.f) % 8;
		return Points[Index];
	}
}

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

void USWGMissionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Network = Collection.InitializeDependency<USWGNetworkSubsystem>();
	ObjectGraph = Collection.InitializeDependency<USWGObjectGraphSubsystem>();
	Tre = Collection.InitializeDependency<USWGTreSubsystem>();
	RadialMenu = Collection.InitializeDependency<USWGRadialMenuSubsystem>();

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGMissionSubsystem::HandleMessageReceived);
	}
	if (RadialMenu)
	{
		RadialMenu->OnMissionTerminalUsed.AddDynamic(this, &USWGMissionSubsystem::HandleMissionTerminalUsed);
	}
}

void USWGMissionSubsystem::Deinitialize()
{
	if (Network)
	{
		Network->OnMessageReceived.Remove(MessageHandle);
	}
	if (RadialMenu)
	{
		RadialMenu->OnMissionTerminalUsed.RemoveDynamic(this, &USWGMissionSubsystem::HandleMissionTerminalUsed);
	}
	Super::Deinitialize();
}

void USWGMissionSubsystem::HandleMissionTerminalUsed(int64 TerminalObjectId)
{
	ActiveTerminalId = TerminalObjectId;
	SendMissionListRequest(TerminalObjectId);
	OnMissionWindowRequested.Broadcast(TerminalObjectId);

	// The bag may already hold last request's missions (server reuses the same
	// MissionObjects and re-randomizes them) — let the UI draw what we have
	// right away rather than sitting blank until the next delta arrives.
	OnMissionListChanged.Broadcast();
}

bool USWGMissionSubsystem::SendMissionListRequest(int64 TerminalObjectId)
{
	if (!Network || !ObjectGraph || TerminalObjectId == 0)
	{
		return false;
	}
	const int64 PlayerId = ObjectGraph->GetLocalPlayerObjectId();
	if (PlayerId == 0)
	{
		return false;
	}

	++NextRequestSeq;
	FMissionListRequest Request(PlayerId, TerminalObjectId, NextRequestSeq);
	Network->SendMessage(Request.Serialize());
	LastRequestSeq = NextRequestSeq;
	UE_LOG(LogSWGMission, Log, TEXT("mission list requested for terminal %lld (seq %u)"), TerminalObjectId, NextRequestSeq);
	return true;
}

int64 USWGMissionSubsystem::FindMissionBagId() const
{
	if (MissionBagId != 0 || !ObjectGraph || !Tre)
	{
		return MissionBagId;
	}

	for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(ObjectGraph->GetLocalPlayerObjectId()))
	{
		if (const ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(ObjectGraph->FindActor(ObjectId)))
		{
			if (Tre->ResolveTemplatePath(NetObject->GetObjectCrc()).Contains(TEXT("mission_bag")))
			{
				MissionBagId = ObjectId;
				break;
			}
		}
	}
	return MissionBagId;
}

TArray<FSWGMissionEntry> USWGMissionSubsystem::GetMissions() const
{
	TArray<FSWGMissionEntry> Result;
	if (!ObjectGraph)
	{
		return Result;
	}

	const int64 BagId = FindMissionBagId();
	if (BagId == 0)
	{
		return Result;
	}

	// Raw-space position, not SWGToUnrealSpace's UE-space one: CompassDirection and
	// the distance below both want to stay in the same (x east, y north) frame the
	// mission's own StartPositionRaw is already in, so there's no axis-swap to undo.
	const AActor* PlayerActor = ObjectGraph->FindActor(ObjectGraph->GetLocalPlayerObjectId());
	TOptional<FVector> PlayerRaw;
	if (PlayerActor)
	{
		PlayerRaw = SWGToRawSpace(PlayerActor->GetActorLocation());
	}

	for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(BagId))
	{
		const FSWGMissionEntry* Found = Entries.Find(ObjectId);
		if (!Found)
		{
			continue;
		}

		// The bag is shared by every terminal type, and a terminal only
		// randomizes its own slot range — without this, using any terminal
		// would list every OTHER terminal's leftover missions too. Only
		// entries this exact request just (re)populated count.
		if (LastRequestSeq == 0 || Found->RefreshCounter != LastRequestSeq)
		{
			continue;
		}

		FSWGMissionEntry Entry = *Found;

		if (PlayerRaw.IsSet() && Entry.bHasStartPosition)
		{
			// Assumes the mission is on the player's current planet — nothing here checks StartPlanetCrc yet.
			Entry.DistanceMeters = FVector::Dist2D(*PlayerRaw, Entry.StartPositionRaw);
			Entry.Direction = CompassDirection(*PlayerRaw, Entry.StartPositionRaw, Entry.BearingDegrees);
		}

		Result.Add(Entry);
	}
	return Result;
}

TArray<FSWGMissionEntry> USWGMissionSubsystem::GetMissionsWithWaypoints() const
{
	// No containment/BagId check: unlike GetMissions() (which needs FindMissionBagId
	// to tell one terminal's offers apart from another's), a MISO baseline/delta only
	// ever arrives for something already in the local player's own mission_bag — the
	// server has no reason to send one otherwise. FindMissionBagId also depends on
	// the bag itself having a spawned actor (to read its template CRC), which bags
	// apparently never get, so relying on it here would just always come up empty.
	TArray<FSWGMissionEntry> Result;
	for (const TPair<int64, FSWGMissionEntry>& Pair : Entries)
	{
		if (Pair.Value.bHasWaypoint)
		{
			Result.Add(Pair.Value);
		}
	}
	return Result;
}

TArray<FSWGMissionEntry> USWGMissionSubsystem::GetAllTrackedMissions() const
{
	TArray<FSWGMissionEntry> Result;
	Entries.GenerateValueArray(Result);
	return Result;
}

bool USWGMissionSubsystem::RefreshMissionList()
{
	return SendMissionListRequest(ActiveTerminalId);
}

bool USWGMissionSubsystem::AcceptMission(int64 MissionObjectId)
{
	if (!Network || !ObjectGraph || ActiveTerminalId == 0 || MissionObjectId == 0)
	{
		return false;
	}
	const int64 PlayerId = ObjectGraph->GetLocalPlayerObjectId();
	if (PlayerId == 0)
	{
		return false;
	}

	FMissionAccept Accept(PlayerId, MissionObjectId, ActiveTerminalId);
	Network->SendMessage(Accept.Serialize());
	UE_LOG(LogSWGMission, Log, TEXT("mission %lld accepted at terminal %lld"), MissionObjectId, ActiveTerminalId);
	return true;
}

void USWGMissionSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message)
	{
		return;
	}

	switch (static_cast<ESWGMessageOp>(Message->Opcode))
	{
		case ESWGMessageOp::BaselinesMessage:
		{
			const FBaselinesMessage& Baselines = *static_cast<const FBaselinesMessage*>(Message.Get());
			if (Baselines.GetObjectType() != ESWGObjectType::MISO || Baselines.BaselineType != 3)
			{
				break;
			}

			FSWGPacket Packet = Baselines.AsPayloadPacket();
			FMissionObjectBaseline Baseline;
			SWGMissionBaselineParser::ParseBase3(Packet, Baseline);

			FSWGMissionEntry& Entry = Entries.FindOrAdd(Baselines.ObjectId);
			Entry.ObjectId = Baselines.ObjectId;
			Entry.TargetName = Baseline.TargetName;
			Entry.RewardCredits = Baseline.RewardCredits;
			Entry.DifficultyDisplay = Baseline.DifficultyDisplay;
			Entry.TypeCRC = static_cast<int32>(Baseline.TypeCRC);
			if (Tre && !Baseline.MissionTitle.StringTableId.IsEmpty())
			{
				Entry.Title = FText::FromString(Tre->LookupString(Baseline.MissionTitle.File, Baseline.MissionTitle.StringTableId));
			}
			if (Tre && !Baseline.MissionDescription.StringTableId.IsEmpty())
			{
				Entry.Description = FText::FromString(Tre->LookupString(Baseline.MissionDescription.File, Baseline.MissionDescription.StringTableId));
			}
			if (Baseline.TargetTemplateCrc != 0)
			{
				Entry.TargetTemplateCrc = static_cast<int32>(Baseline.TargetTemplateCrc);
				if (Tre)
				{
					Entry.TargetTemplateName = FText::FromString(Tre->ResolveTemplateObjectName(Baseline.TargetTemplateCrc));
				}
			}
			// WaypointObjectId alone isn't a reliable "has a real waypoint" signal — the
			// baseline's "no waypoint yet" placeholder (every mission_bag slot starts
			// this way, offered or not) still carries a non-zero id and a plausible-
			// looking WaypointName (the mission's own type string). WaypointActive is
			// what retail actually keys "was this waypoint granted" on: a mission
			// doesn't get an active waypoint until it's accepted.
			Entry.bHasWaypoint = Baseline.WaypointActive != 0;
			if (Entry.bHasWaypoint)
			{
				Entry.WaypointObjectId = Baseline.WaypointObjectId;
				Entry.WaypointName = Baseline.WaypointName;
				Entry.WaypointPlanetCrc = static_cast<int32>(Baseline.WaypointPlanetCrc);
				Entry.WaypointColor = Baseline.WaypointColor;
				Entry.bWaypointActive = true;
				Entry.WaypointRawPosition = Baseline.WaypointPosition;
			}

			OnMissionListChanged.Broadcast();
			break;
		}
		case ESWGMessageOp::DeltasMessage:
		{
			const FDeltasMessage& Deltas = *static_cast<const FDeltasMessage*>(Message.Get());
			if (Deltas.GetObjectType() != ESWGObjectType::MISO || Deltas.DeltaType != 3)
			{
				break;
			}

			FSWGPacket Packet = Deltas.AsPayloadPacket();
			FMissionObjectDelta Delta;
			SWGMissionDeltaParser::ParseDelta3(Packet, Delta, Deltas.UpdateCount);

			// The baseline for a fresh MISO always arrives first (it's a normal
			// SceneCreateObjectByCrc'd object) — FindOrAdd here just tolerates a
			// delta that beat the baseline over the wire rather than dropping it.
			FSWGMissionEntry& Entry = Entries.FindOrAdd(Deltas.ObjectId);
			Entry.ObjectId = Deltas.ObjectId;

			if (Delta.RewardCredits.IsSet())      { Entry.RewardCredits = *Delta.RewardCredits; }
			if (Delta.DifficultyDisplay.IsSet())  { Entry.DifficultyDisplay = *Delta.DifficultyDisplay; }
			if (Delta.TypeCRC.IsSet())            { Entry.TypeCRC = static_cast<int32>(*Delta.TypeCRC); Entry.bPopulated = true; }
			if (Delta.RefreshCounter.IsSet())     { Entry.RefreshCounter = *Delta.RefreshCounter; }
			if (Delta.TargetName.IsSet())         { Entry.TargetName = *Delta.TargetName; }
			if (Delta.MissionTitle.IsSet() && Tre)
			{
				Entry.Title = FText::FromString(Tre->LookupString(Delta.MissionTitle->File, Delta.MissionTitle->StringTableId));
			}
			if (Delta.MissionDescription.IsSet() && Tre)
			{
				Entry.Description = FText::FromString(Tre->LookupString(Delta.MissionDescription->File, Delta.MissionDescription->StringTableId));
			}
			if (Delta.TargetTemplateCrc.IsSet())
			{
				Entry.TargetTemplateCrc = static_cast<int32>(*Delta.TargetTemplateCrc);
				if (Tre)
				{
					Entry.TargetTemplateName = FText::FromString(Tre->ResolveTemplateObjectName(*Delta.TargetTemplateCrc));
				}
			}
			if (Delta.StartPosition.IsSet())
			{
				Entry.StartPositionRaw = *Delta.StartPosition;
				Entry.bHasStartPosition = true;
			}
			// See the BaselinesMessage case above: WaypointActive, not a non-zero
			// WaypointObjectId, is the real "has a granted waypoint" signal.
			if (Delta.WaypointActive.IsSet())
			{
				Entry.bHasWaypoint = *Delta.WaypointActive != 0;
				Entry.bWaypointActive = Entry.bHasWaypoint;
			}
			if (Delta.WaypointObjectId.IsSet())  { Entry.WaypointObjectId = *Delta.WaypointObjectId; }
			if (Delta.WaypointName.IsSet())      { Entry.WaypointName = *Delta.WaypointName; }
			if (Delta.WaypointPlanetCrc.IsSet()) { Entry.WaypointPlanetCrc = static_cast<int32>(*Delta.WaypointPlanetCrc); }
			if (Delta.WaypointColor.IsSet())     { Entry.WaypointColor = *Delta.WaypointColor; }
			if (Delta.WaypointPosition.IsSet())  { Entry.WaypointRawPosition = *Delta.WaypointPosition; }

			OnMissionListChanged.Broadcast();
			break;
		}
		case ESWGMessageOp::UpdateContainmentMessage:
		{
			const FUpdateContainmentMessage& Containment = *static_cast<const FUpdateContainmentMessage*>(Message.Get());
			const int64 BagId = FindMissionBagId();
			if (BagId != 0 && (Containment.ContainerId == BagId || Entries.Contains(Containment.ObjectId)))
			{
				OnMissionListChanged.Broadcast();
			}
			break;
		}
		default:
			break;
	}
}
