#include "Subsystems/SWGPerformanceAudioSubsystem.h"

#include "Algo/AnyOf.h"
#include "Components/SWGPerformanceComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/ObjControllerMessageIn.h"
#include "Network/Messages/Zone/Object/FlourishIn.h"
#include "SWGAudioSubsystem.h"
#include "SWGAudioTypes.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	const TCHAR* PerformanceTablePath = TEXT("datatables/performance/performance.iff");

	// The next segment is started this early so the one-shots abut instead of leaving a frame of silence.
	constexpr double SegmentLeadSeconds = 0.03;

	// A segment this far behind its boundary (decode stall, hitch) restarts the timeline from now.
	constexpr double MaxLateSeconds = 0.25;

	// A joiner within this of a grid boundary just starts, rather than waiting a whole loop.
	constexpr double GridToleranceSeconds = 0.15;

	constexpr float DefaultLoopSeconds = 5.f;

	constexpr float StopFadeSeconds = 0.3f;

	USceneComponent* FindAttachComponent(AActor& Actor)
	{
		const ACharacter* Character = Cast<ACharacter>(&Actor);
		return Character && Character->GetMesh() ? static_cast<USceneComponent*>(Character->GetMesh()) : Actor.GetRootComponent();
	}
}

void USWGPerformanceAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Network = Cast<USWGNetworkSubsystem>(Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));
	ObjectGraph = Cast<USWGObjectGraphSubsystem>(Collection.InitializeDependency(USWGObjectGraphSubsystem::StaticClass()));
	Audio = Cast<USWGAudioSubsystem>(Collection.InitializeDependency(USWGAudioSubsystem::StaticClass()));
	Tre = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGPerformanceAudioSubsystem::HandleMessageReceived);
	}
	PerformanceChangedHandle = USWGPerformanceComponent::OnPerformanceChanged.AddUObject(this, &USWGPerformanceAudioSubsystem::HandlePerformanceChanged);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USWGPerformanceAudioSubsystem::Tick));
}

void USWGPerformanceAudioSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	USWGPerformanceComponent::OnPerformanceChanged.Remove(PerformanceChangedHandle);
	if (Network && MessageHandle.IsValid())
	{
		Network->OnMessageReceived.Remove(MessageHandle);
		MessageHandle.Reset();
	}

	for (TPair<TWeakObjectPtr<AActor>, FPerformer>& Entry : Performers)
	{
		StopPerformer(Entry.Value, 0.f);
	}
	Performers.Reset();
	SongGrids.Reset();
	Network = nullptr;
	ObjectGraph = nullptr;
	Audio = nullptr;
	Tre = nullptr;
	Super::Deinitialize();
}

const FSWGPerformanceTable* USWGPerformanceAudioSubsystem::GetTable()
{
	if (!bTableLoadAttempted && Tre)
	{
		bTableLoadAttempted = true;
		Table = MakeShared<FSWGPerformanceTable>();
		if (!Tre->FileExists(PerformanceTablePath) || !FSWGPerformanceTableReader::Read(Tre->CreateIffReader(PerformanceTablePath), *Table))
		{
			UE_LOG(LogSWGAudio, Warning, TEXT("could not read %s - entertainer music is off"), PerformanceTablePath);
			Table.Reset();
		}
	}
	return Table.Get();
}

int32 USWGPerformanceAudioSubsystem::GetPerformanceIndex(const AActor* Performer) const
{
	const FPerformer* Found = Performers.Find(const_cast<AActor*>(Performer));
	return Found && !Found->bEnding ? Found->PerformanceIndex : 0;
}

// -- Inbound ----------------------------------------------------------------

void USWGPerformanceAudioSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message || Message->Opcode != static_cast<uint32>(ESWGMessageOp::ObjControllerMessage))
	{
		return;
	}

	const FObjControllerMessageIn& Envelope = *static_cast<const FObjControllerMessageIn*>(Message.Get());
	if (Envelope.GetSubOp() != ESWGObjControllerOp::Flourish)
	{
		return;
	}

	FSWGPacket Payload = Envelope.AsPayloadPacket();
	FFlourishIn Flourish;
	if (!Flourish.Parse(Payload))
	{
		UE_LOG(LogSWGAudio, Warning, TEXT("malformed Flourish (%d payload bytes)"), Envelope.RawPayload.Num());
		return;
	}

	PerformFlourish(ObjectGraph ? ObjectGraph->FindActor(Envelope.ObjectId) : nullptr, Flourish.FlourishId);
}

void USWGPerformanceAudioSubsystem::HandlePerformanceChanged(USWGPerformanceComponent& Component)
{
	const UGameInstance* GameInstance = GetGameInstance();
	AActor* Owner = Component.GetOwner();
	if (Owner && GameInstance && Owner->GetWorld() == GameInstance->GetWorld())
	{
		SetPerformance(Owner, Component.PerformanceType);
	}
}

// -- Performance control ------------------------------------------------------

void USWGPerformanceAudioSubsystem::SetPerformance(AActor* Performer, int32 PerformanceIndex)
{
	if (!Performer || !Audio)
	{
		return;
	}

	const TWeakObjectPtr<AActor> Key(Performer);
	const FSWGPerformanceTable* PerformanceTable = GetTable();
	const FSWGPerformanceRow* Row = PerformanceTable ? PerformanceTable->FindByIndex(PerformanceIndex) : nullptr;
	const bool bPlaysMusic = Row && Row->IsMusic() && !Row->MainLoop.IsEmpty();

	if (FPerformer* Existing = Performers.Find(Key))
	{
		// Baseline and delta both report the same performance.
		if (bPlaysMusic && Existing->PerformanceIndex == PerformanceIndex && !Existing->bEnding)
		{
			return;
		}

		// Stopping after a -1 flourish: let the outro that is already playing finish.
		if (!bPlaysMusic && Existing->Segment == ESegment::Outro)
		{
			Existing->bEnding = true;
			return;
		}

		StopPerformer(*Existing, StopFadeSeconds);
		Performers.Remove(Key);
		ReleaseGrids();
	}

	if (!bPlaysMusic)
	{
		return;
	}

	FPerformer& Started = Performers.Add(Key);
	Started.PerformanceIndex = PerformanceIndex;
	Started.SongKey = Row->Name.ToLower();
	// PerformanceStartTime is always 0 from the server, so the song starts now.
	Started.SegmentEnd = FPlatformTime::Seconds();
	UE_LOG(LogSWGAudio, Log, TEXT("%s performs %s (%s, row %d)"), *Performer->GetName(), *Row->Name, *Row->RequiredInstrument, PerformanceIndex);
	AdvanceSegment(*Performer, Started);
}

void USWGPerformanceAudioSubsystem::PerformFlourish(AActor* Performer, int32 FlourishId)
{
	FPerformer* Found = Performer ? Performers.Find(Performer) : nullptr;
	if (!Found)
	{
		return;
	}

	if (FlourishId == -1)
	{
		Found->bOutroQueued = true;
	}
	else if (FlourishId >= 1 && FlourishId <= 8)
	{
		Found->PendingFlourish = FlourishId;
	}
}

void USWGPerformanceAudioSubsystem::StopPerformer(FPerformer& Performer, float FadeOutSeconds)
{
	Performer.Handle.Stop(FadeOutSeconds);
	Performer.Handle = FSWGSoundHandle();
}

void USWGPerformanceAudioSubsystem::ReleaseGrids()
{
	for (auto Iterator = SongGrids.CreateIterator(); Iterator; ++Iterator)
	{
		const bool bInUse = Algo::AnyOf(Performers, [&Iterator](const TPair<TWeakObjectPtr<AActor>, FPerformer>& Entry)
			{
				return Entry.Value.SongKey == Iterator.Key();
			});
		if (!bInUse)
		{
			Iterator.RemoveCurrent();
		}
	}
}

// -- Segment sequencing --------------------------------------------------------

bool USWGPerformanceAudioSubsystem::Tick(float DeltaSeconds)
{
	if (Performers.IsEmpty())
	{
		return true;
	}

	const double Now = FPlatformTime::Seconds();
	bool bRemoved = false;
	for (auto Iterator = Performers.CreateIterator(); Iterator; ++Iterator)
	{
		AActor* Actor = Iterator.Key().Get();
		FPerformer& Performer = Iterator.Value();
		if (!Actor)
		{
			StopPerformer(Performer, 0.f);
			Iterator.RemoveCurrent();
			bRemoved = true;
			continue;
		}

		if (Performer.bAwaitingDuration)
		{
			BeginSegment(*Actor, Performer, Performer.AwaitedSegment, Performer.AwaitedSound);
		}
		else if (Now >= Performer.SegmentEnd - SegmentLeadSeconds && Performer.Segment != ESegment::Finished)
		{
			AdvanceSegment(*Actor, Performer);
		}

		// The server ended the performance and the outro has run its course.
		if (Performer.bEnding && Performer.Segment == ESegment::Finished)
		{
			Iterator.RemoveCurrent();
			bRemoved = true;
		}
	}

	if (bRemoved)
	{
		ReleaseGrids();
	}
	return true;
}

void USWGPerformanceAudioSubsystem::AdvanceSegment(AActor& Actor, FPerformer& Performer)
{
	const FSWGPerformanceTable* PerformanceTable = GetTable();
	const FSWGPerformanceRow* Row = PerformanceTable ? PerformanceTable->FindByIndex(Performer.PerformanceIndex) : nullptr;
	if (!Row || Performer.Segment == ESegment::Outro)
	{
		Performer.Segment = ESegment::Finished;
		return;
	}

	if (Performer.bOutroQueued)
	{
		BeginSegment(Actor, Performer, ESegment::Outro, Row->Outro);
		return;
	}

	if (Performer.Segment == ESegment::Start && !Row->Intro.IsEmpty())
	{
		BeginSegment(Actor, Performer, ESegment::Intro, Row->Intro);
		return;
	}

	if (Performer.PendingFlourish > 0)
	{
		const FString& FlourishSound = Row->Flourish[Performer.PendingFlourish - 1];
		Performer.PendingFlourish = 0;
		if (!FlourishSound.IsEmpty())
		{
			BeginSegment(Actor, Performer, ESegment::Flourish, FlourishSound);
			return;
		}
	}

	// Band play: a later performer of the same song waits for the first one's loop boundary.
	const bool bFirstLoop = Performer.Segment == ESegment::Start || Performer.Segment == ESegment::Intro;
	const FSongGrid* Grid = bFirstLoop ? SongGrids.Find(Performer.SongKey) : nullptr;
	if (Grid && Grid->Period > 0.0)
	{
		double Phase = FMath::Fmod(Performer.SegmentEnd - Grid->Start, Grid->Period);
		Phase += Phase < 0.0 ? Grid->Period : 0.0;
		if (Phase > GridToleranceSeconds && Grid->Period - Phase > GridToleranceSeconds)
		{
			Performer.Segment = ESegment::Waiting;
			Performer.SegmentEnd += Grid->Period - Phase;
			return;
		}
	}

	BeginSegment(Actor, Performer, ESegment::Main, Row->MainLoop);
}

void USWGPerformanceAudioSubsystem::BeginSegment(AActor& Actor, FPerformer& Performer, ESegment Segment, const FString& SndPath)
{
	float Seconds = -1.f;
	if (!SndPath.IsEmpty() && !FindSoundSeconds(SndPath, Seconds))
	{
		Performer.bAwaitingDuration = true;
		Performer.AwaitedSegment = Segment;
		Performer.AwaitedSound = SndPath;
		return;
	}
	Performer.bAwaitingDuration = false;

	if (Seconds <= 0.f)
	{
		const FSWGPerformanceTable* PerformanceTable = GetTable();
		const FSWGPerformanceRow* Row = PerformanceTable ? PerformanceTable->FindByIndex(Performer.PerformanceIndex) : nullptr;
		Seconds = Row && Row->LoopDurationSeconds > 0.f ? Row->LoopDurationSeconds : DefaultLoopSeconds;
	}

	// Chain on the previous boundary so the loops do not drift by frame time.
	const double Now = FPlatformTime::Seconds();
	const double Start = Now - Performer.SegmentEnd > MaxLateSeconds ? Now : Performer.SegmentEnd;

	if (Segment == ESegment::Main && !SongGrids.Contains(Performer.SongKey))
	{
		SongGrids.Add(Performer.SongKey, FSongGrid{ Start, Seconds });
	}

	if (!SndPath.IsEmpty() && IsAudible(Actor, SndPath))
	{
		FSWGSoundPlayOptions Options;
		Options.bForceSpatial = true;
		Performer.Handle = Audio->PlaySoundAttached(SndPath, FindAttachComponent(Actor), NAME_None, Options);
	}

	Performer.Segment = Segment;
	Performer.SegmentEnd = Start + Seconds;
}

bool USWGPerformanceAudioSubsystem::FindSoundSeconds(const FString& SndPath, float& OutSeconds)
{
	if (const float* Settled = SoundSeconds.Find(SndPath))
	{
		OutSeconds = *Settled;
		return true;
	}

	const TSharedPtr<const FSWGSoundTemplate> Template = Audio ? Audio->GetSoundTemplate(SWGAudio::NormalizeSoundPath(SndPath)) : nullptr;
	if (!Template.IsValid() || Template->Samples.IsEmpty())
	{
		SoundSeconds.Add(SndPath, -1.f);
		OutSeconds = -1.f;
		return true;
	}

	if (!PendingSoundSeconds.Contains(SndPath))
	{
		PendingSoundSeconds.Add(SndPath);
		TWeakObjectPtr<USWGPerformanceAudioSubsystem> WeakThis(this);
		Audio->RequestPcm(Template->Samples[0], [WeakThis, SndPath](TSharedPtr<const FSWGDecodedPcm> Pcm)
			{
				if (USWGPerformanceAudioSubsystem* Subsystem = WeakThis.Get())
				{
					Subsystem->PendingSoundSeconds.Remove(SndPath);
					Subsystem->SoundSeconds.Add(SndPath, Pcm && Pcm->IsValid() ? Pcm->GetDurationSeconds() : -1.f);
				}
			});
	}

	if (const float* Settled = SoundSeconds.Find(SndPath))
	{
		OutSeconds = *Settled;
		return true;
	}
	return false;
}

bool USWGPerformanceAudioSubsystem::IsAudible(const AActor& Actor, const FString& SndPath) const
{
	// Music .snd files are 2D, so force the spatial radius a sound object would get.
	const float Radius = Audio->GetAudibleRadius(SndPath, true);
	if (Radius <= 0.f)
	{
		return Radius < 0.f;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const APlayerController* Controller = GameInstance && GameInstance->GetWorld() ? GameInstance->GetWorld()->GetFirstPlayerController() : nullptr;
	return !Controller || FVector::Dist(Controller->GetFocalLocation(), Actor.GetActorLocation()) <= Radius;
}
