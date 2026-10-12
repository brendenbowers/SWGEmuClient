#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Playback/SWGSoundHandle.h"
#include "TRE/SWGPerformanceTableReader.h"
#include "SWGPerformanceAudioSubsystem.generated.h"

class AActor;
class USWGAudioSubsystem;
class USWGNetworkSubsystem;
class USWGObjectGraphSubsystem;
class USWGPerformanceComponent;
class USWGTreSubsystem;
struct FSWGNetMessage;

/**
 * Entertainer music. A performance.iff row's samples are one-shots lasting one loop each, so the
 * client re-triggers them back to back: intro, then mainloop repeated, with a queued flourish
 * replacing one mainloop iteration, and the outro on a -1 flourish.
 *
 * A subsystem rather than a creature component: it needs one shared ticker, the per-song grid that
 * keeps band members in step, and the ObjController flourish routing.
 *
 * The server never sets PerformanceStartTime (always 0), so there is no start reference to
 * join mid-loop from. Performers of the same song instead share a client-side grid, so a
 * later joiner's mainloop starts on the first performer's loop boundary.
 */
UCLASS()
class SWGEMUCLIENT_API USWGPerformanceAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The parsed performance.iff, loaded on first use. Null if it could not be read. */
	const FSWGPerformanceTable* GetTable();

	/** Starts (or restarts) the performance with this 1-based performance.iff index on Performer; 0 or a dance ends it. */
	void SetPerformance(AActor* Performer, int32 PerformanceIndex);

	/** 1-8 queues that flourish for the next loop boundary; -1 queues the outro. */
	void PerformFlourish(AActor* Performer, int32 FlourishId);

	int32 GetPerformanceIndex(const AActor* Performer) const;
	int32 GetActivePerformerCount() const { return Performers.Num(); }

private:
	/** Start is the state before anything has played; Waiting is silence until the song grid's next boundary. */
	enum class ESegment : uint8 { Start, Intro, Waiting, Main, Flourish, Outro, Finished };

	struct FPerformer
	{
		int32 PerformanceIndex = 0;
		ESegment Segment = ESegment::Start;
		FSWGSoundHandle Handle;
		/** FPlatformTime seconds at which the running segment ends and the next one starts. */
		double SegmentEnd = 0.0;
		int32 PendingFlourish = 0;
		bool bOutroQueued = false;
		/** The server ended the performance; the entry lingers only while the outro plays. */
		bool bEnding = false;
		/** Set while the next segment's duration is still being decoded. */
		bool bAwaitingDuration = false;
		ESegment AwaitedSegment = ESegment::Finished;
		FString AwaitedSound;
		FString SongKey;
	};

	/** A song's shared loop grid: mainloop iterations of every performer of the song start at Start + n * Period. */
	struct FSongGrid
	{
		double Start = 0.0;
		double Period = 0.0;
	};

	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);
	void HandlePerformanceChanged(USWGPerformanceComponent& Component);
	bool Tick(float DeltaSeconds);

	void AdvanceSegment(AActor& Actor, FPerformer& Performer);
	void BeginSegment(AActor& Actor, FPerformer& Performer, ESegment Segment, const FString& SndPath);
	void StopPerformer(FPerformer& Performer, float FadeOutSeconds);
	void ReleaseGrids();

	/** Seconds the .snd's first sample lasts (-1 if unusable). False while it is still decoding. */
	bool FindSoundSeconds(const FString& SndPath, float& OutSeconds);
	bool IsAudible(const AActor& Actor, const FString& SndPath) const;

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGAudioSubsystem> Audio;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	TSharedPtr<FSWGPerformanceTable> Table;
	bool bTableLoadAttempted = false;

	TMap<TWeakObjectPtr<AActor>, FPerformer> Performers;
	TMap<FString, FSongGrid> SongGrids;
	/** Seconds per .snd path, -1 once a decode failed. */
	TMap<FString, float> SoundSeconds;
	TSet<FString> PendingSoundSeconds;

	FDelegateHandle MessageHandle;
	FDelegateHandle PerformanceChangedHandle;
	FTSTicker::FDelegateHandle TickHandle;
};
