#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGSkeletonReader.h"
#include "TRE/SWGMeshReader.h"
#include "TRE/SWGAnimationReader.h"
#include "Import/SWGSkeletalMeshImporter.h"
#include "Import/SWGAnimationImporter.h"
#include "Containers/Queue.h"
#include "Async/Future.h"
#include <atomic>

class USWGMeshGeneratorSubsystem;
class USkeletalMesh;
class USkeleton;
class UAnimSequence;
class UBlendSpace;
class UMeshComponent;
class USkeletalMeshComponent;
class UAnimSingleNodeInstance;

/**
 * One actor's live animation playback — a real UBlendSpace played on
 * Character->GetMesh() via UAnimSingleNodeInstance, driven every tick by the
 * actor's current horizontal speed. See FSWGSkeletalAnimationPipeline::
 * TryApplyGeneratedAnimatedMesh.
 */
struct FSWGPlayingAnimation
{
	TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
	TWeakObjectPtr<UAnimSingleNodeInstance> AnimInstance;
};

/** One queued skeletal-mesh build request — everything the worker pump needs
 *  is plain data, copied in at request time so the pump never touches the
 *  requesting call stack. */
struct FSWGSkeletalMeshBuildRequest
{
	FString SkeletonPath;
	TArray<FString> MeshVirtualPaths;
	FSWGSkeletonData Skeleton;
	FString PackagePath;
	// GetSlotHardpoints() lazily parses slot_definitions.iff on first use — a
	// game-thread-only state mutation — so it must be resolved once at
	// request time (game thread) and carried here, not re-resolved inside
	// the worker pump.
	TMap<FString, FString> SlotHardpoints;
	// TODO: fix the tsharedptr here — see FSWGSkeletalMeshBuildResult::Promise for why this is a TSharedPtr instead of a plain TPromise.
	TSharedPtr<TPromise<USkeletalMesh*>> Promise;
};

/** One finished (or failed) skeletal-mesh build, waiting on
 *  CompletedSkeletalMeshBuilds for Tick to finalize on the game thread. */
struct FSWGSkeletalMeshBuildResult
{
	bool bBuilt = false;
	FSWGSkeletalMeshBuildData BuildData; // only meaningful if bBuilt
	FString PackagePath;
	FString SkeletonPath;
	TArray<FString> MeshVirtualPaths;
	// See FSWGSkeletalMeshBuildRequest::Promise for why this is a TSharedPtr.
	TSharedPtr<TPromise<USkeletalMesh*>> Promise;
};

/** Same shape as FSWGSkeletalMeshBuildRequest, for locomotion anim sequences.
 *  ClipAnimation is decoded synchronously at request time (TreSubsystem
 *  reads are thread-safe, but keeping the decode on the game thread here
 *  means RequestLocomotionAnimSequence can report a decode failure
 *  immediately instead of round-tripping through the queue first). */
struct FSWGAnimSequenceBuildRequest
{
	FString SkeletonPath;
	TArray<FString> MeshVirtualPaths;
	FString ClipPath;
	FSWGSkeletonData Skeleton;
	FSWGAnimationData ClipAnimation;
	USkeleton* TargetSkeleton = nullptr;
	FString PackagePath;
	// See FSWGSkeletalMeshBuildRequest::Promise for why this is a TSharedPtr.
	TSharedPtr<TPromise<UAnimSequence*>> Promise;
};

/** Same shape as FSWGSkeletalMeshBuildResult, for locomotion anim sequences. */
struct FSWGAnimSequenceBuildResult
{
	bool bBuilt = false;
	FSWGAnimSequenceBuildData BuildData;
	FString PackagePath;
	FString ClipPath;
	USkeleton* TargetSkeleton = nullptr;
	// See FSWGSkeletalMeshBuildRequest::Promise for why this is a TSharedPtr.
	TSharedPtr<TPromise<UAnimSequence*>> Promise;
};

/**
 * Owns the async skeletal-mesh + locomotion-animation generation pipeline *
 */
class SWGEMUCLIENT_API FSWGSkeletalAnimationPipeline
{
public:
	explicit FSWGSkeletalAnimationPipeline(USWGMeshGeneratorSubsystem& InOwner);
	~FSWGSkeletalAnimationPipeline();

	/** Feeds each actor's live horizontal speed into its blend space every
	 *  tick, drains a budgeted number of finished builds (finalizing them on
	 *  the game thread), and re-arms the worker pump(s) if there's queued
	 *  work and backpressure allows. Called from
	 *  USWGMeshGeneratorSubsystem::Tick. */
	void Tick(float DeltaTime);

	/**
	 * Generic per-species resolution: works for any ACharacter whose resolved
	 * mesh/skeleton data yields a SKTM skeleton reference and a usable LATX
	 * locomotion LAT. Swaps in the generated skeletal mesh directly on
	 * Character->GetMesh()
	 */
	void TryApplyGeneratedAnimatedMesh(AActor& Actor, const TArray<FString>& MeshVirtualPaths, const TMap<FString, FString>& AnimationLatPaths, UMeshComponent* ProceduralMeshComponent, const TMap<FString, FLinearColor>* PaletteTintOverrides = nullptr, const TMap<FString, float>* MorphWeights = nullptr, const TMap<FString, int32>* TextureIndexOverrides = nullptr);

	/**
	 * Loads the once-built USkeletalMesh for this skeleton+mesh-parts
	 * combination (package name hashed from SkeletonPath + MeshVirtualPaths,
	 * under /Game/SWGEmu/Generated/), or builds and saves it via
	 * FSWGSkeletalMeshImporter if it doesn't exist yet and this is an
	 * editor/PIE build. The saved .uasset on disk *is* the cache — a later
	 * LoadObject call is the cache-hit path, returned as an already-resolved
	 * future. If MeshVirtualPaths includes a "*_head*" part, also looks for a
	 * matching "<prefix>_face.skt" skeleton and merges it under the "head"
	 * joint for the mesh build only.
	 *
	 * On a cache miss, the request is queued and the
	 * returned future resolves once a worker pump processes it and Tick
	 * finalizes the result
	 */
	TFuture<USkeletalMesh*> RequestGeneratedSkeletalMesh(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FSWGSkeletonData& Skeleton);

private:
	/**
	 * Same cache-hit/queue/backpressure shape as RequestGeneratedSkeletalMesh,
	 * for one locomotion clip. TargetSkeleton is the USkeleton the playing
	 * USkeletalMeshComponent actually uses (may include merged face bones) —
	 * Skeleton is the plain joint list the animated tracks are built from.
	 */
	TFuture<UAnimSequence*> RequestLocomotionAnimSequence(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FString& ClipPath, const FSWGSkeletonData& Skeleton, USkeleton* TargetSkeleton);

	/**
	 * Loads the once-built UBlendSpace for this skeleton+mesh-parts+locomotion-clips
	 * combination, or builds and saves it if missing and this is an
	 * editor/PIE build: a single horizontal-speed axis with idle/walk/run
	 * samples at 0/WalkSpeed/RunSpeed. Synchronous — building a UBlendSpace
	 * is cheap UObject/package work only, no CPU-heavy pass worth threading.
	 */
	UBlendSpace* GetOrBuildLocomotionBlendSpace(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const TArray<FString>& LocomotionPaths, UAnimSequence* IdleSequence, UAnimSequence* WalkSequence, UAnimSequence* RunSequence, float WalkSpeed, float RunSpeed, USkeleton* TargetSkeleton);

	/** Slot name -> hardpoint socket name, parsed once on first use from
	 *  abstract/slot/slot_definition/slot_definitions.iff — a small,
	 *  session-global reference table shared by every generated skeletal
	 *  mesh, not per-mesh data. */
	const TMap<FString, FString>& GetSlotHardpoints();

	/** Self-looping worker pump for skeletal mesh builds — dispatched via
	 *  Async(EAsyncExecution::ThreadPool, ...) from Tick, keeps pulling from
	 *  PendingSkeletalMeshRequests and building until the queue drains,
	 *  backpressure trips, or bShuttingDown is set. */
	void PumpSkeletalMeshWork();
	/** Same shape as PumpSkeletalMeshWork, for locomotion anim sequences. */
	void PumpAnimSequenceWork();

	/** Finalizes up to MaxToProcess finished skeletal mesh builds on the
	 *  game thread (NewObject/package/asset-registry work) and resolves
	 *  their promises — called from Tick. */
	void DrainCompletedSkeletalMeshBuilds(int32 MaxToProcess);
	/** Same shape as DrainCompletedSkeletalMeshBuilds, for anim sequences. */
	void DrainCompletedAnimSequenceBuilds(int32 MaxToProcess);

	/** Privileged access to TreSubsystem/MeshUtilities/GetOrBuildObjectMaterial
	 *  — see this class's own header comment for why this isn't a narrower
	 *  interface. Never null after construction (constructed by the
	 *  subsystem itself, referencing itself). */
	USWGMeshGeneratorSubsystem& Owner;

	TQueue<FSWGSkeletalMeshBuildRequest, EQueueMode::Mpsc> PendingSkeletalMeshRequests;
	TQueue<FSWGSkeletalMeshBuildResult, EQueueMode::Mpsc> CompletedSkeletalMeshBuilds;
	/** Depth of CompletedSkeletalMeshBuilds — tracked separately since
	 *  TQueue has no cheap Num(); this is the actual backpressure signal.
	 * Incremented by the worker pump  when it enqueues a result, decremented 
	 * by DrainCompletedSkeletalMeshBuilds. */
	std::atomic<int32> PendingSkeletalMeshFinalizeCount{0};
	/** Guards against Tick re-arming a second concurrent pump while one is
	 *  already running. */
	std::atomic<bool> bSkeletalMeshWorkerActive{false};

	TQueue<FSWGAnimSequenceBuildRequest, EQueueMode::Mpsc> PendingAnimSequenceRequests;
	TQueue<FSWGAnimSequenceBuildResult, EQueueMode::Mpsc> CompletedAnimSequenceBuilds;
	std::atomic<int32> PendingAnimSequenceFinalizeCount{0};
	std::atomic<bool> bAnimSequenceWorkerActive{false};

	/** Set by the destructor; checked by both pump loops each iteration so
	 *  they exit promptly instead of referencing a half-destroyed *this*. */
	std::atomic<bool> bShuttingDown{false};
	/** Number of worker pump tasks currently running (0, 1, or 2 — one per
	 *  queue). The destructor spin-waits for this to reach 0 before
	 *  returning, since a pump mid-loop still touches *this*. */
	std::atomic<int32> NumActiveWorkerTasks{0};

	/** Above this many already-finished-but-not-yet-finalized builds, the
	 *  worker pump stops pulling new requests until Tick catches up. */
	static constexpr int32 MaxPendingFinalize = 8;
	/** Finalized (NewObject/package/asset-registry work) per queue per Tick —
	 *  bounds how much game-thread work a single frame can absorb even if a
	 *  large backlog just finished on the worker side. */
	static constexpr int32 MaxFinalizePerTick = 2;

	bool bSlotHardpointsLoaded = false;
	TMap<FString, FString> SlotHardpoints;

	/** Actors whose blend space's speed input needs updating every tick —
	 *  see TryApplyGeneratedAnimatedMesh and Tick. */
	TArray<FSWGPlayingAnimation> PlayingAnimations;
};
