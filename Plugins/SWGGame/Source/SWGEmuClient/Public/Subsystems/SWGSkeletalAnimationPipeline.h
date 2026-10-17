#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGSkeletonReader.h"
#include "TRE/SWGMeshReader.h"
#include "TRE/SWGAnimationReader.h"
#include "TRE/SWGAshReader.h"
#include "TRE/SWGLatReader.h"
#include "Import/SWGSkeletalMeshImporter.h"
#include "Import/SWGAnimationImporter.h"
#include "Common/SWGLocomotionResolver.h"
#include "Common/SWGPostureTypes.h"
#include "Common/SWGSatLoopSource.h"
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
class USWGCharacterAnimInstance;

/** How a playing loop's blend space is steered; see FSWGSkeletalAnimationPipeline::DetermineLoopKind. */
enum class ESWGLoopKind : uint8
{
	/** X = horizontal speed. The idle/walk/run ramp every loop used to be. */
	Speed,
	/** X = speed, Y = yaw rate in -1..1: creature loops with YWAT turn-in-place clips. */
	Yaw,
	/** X = speed, Y = movement heading in degrees: ranged combat stances with DRAT strafe ramps. */
	Direction,
};

/**
 * One species' animation data: the .lat that maps logical animation names to
 * .ans clips, and the .ash state hierarchy those names are written against.
 * Loaded once per LAT path and shared by every creature using it.
 */
struct FSWGLocomotionSource
{
	FSWGLatData Lat;
	FSWGAnimationStateHierarchy Hierarchy;
};

/**
 * One actor's live animation playback — a real UBlendSpace looping on the
 * "loop" track of Character->GetMesh()'s USWGCharacterAnimInstance, driven
 * every tick by the actor's speed and heading. See FSWGSkeletalAnimationPipeline::
 * TryApplyGeneratedAnimatedMesh.
 *
 * The rest of the fields are what Tick needs to swap the blend space when the
 * creature's posture or states change (prone, sitting, swimming, ...) —
 * rebuilding one is the same work as building the first, so everything that
 * went into it is kept here rather than re-derived from the actor.
 */
struct FSWGPlayingAnimation
{
	TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
	TWeakObjectPtr<USWGCharacterAnimInstance> AnimInstance;
	TWeakObjectPtr<USkeleton> TargetSkeleton;

	FString SkeletonPath;
	TArray<FString> MeshVirtualPaths;
	FString LatPath;
	FSWGSkeletonData Skeleton;

	/** The clips currently playing — a swap is needed exactly when the posture/state resolve to a different set. */
	FSWGLocomotionClipSet ClipSet;

	/** Posture/states the current LoopAnimationName was resolved from, so Tick can skip the hierarchy walk while they're unchanged. */
	ESWGPosture Posture = ESWGPosture::Invalid;
	int64 StateBitmask = 0;

	/** Rider pose the clips were resolved with — part of the change check because it can land after RIDINGMOUNT (the mount's mesh resolves later). */
	FString RiderPose;

	/** Set while a replacement blend space is being built, so Tick doesn't queue a second one for the same change. */
	bool bSwapInFlight = false;

	/** The blend space currently on the loop track and how Tick steers it. */
	TWeakObjectPtr<UBlendSpace> LoopSpace;
	ESWGLoopKind LoopKind = ESWGLoopKind::Speed;

	/** World time a posture-change transition clip finishes at; actions wait for it rather than cut it short. */
	float TransitionEndTime = 0.0f;

	bool bHoldingDatapad = false;

	/** Steering state: smoothed yaw rate (degrees/s), last actor yaw, and the last movement heading used while stationary. */
	float SmoothedYawRate = 0.0f;
	float PreviousActorYaw = 0.0f;
	bool bHasPreviousActorYaw = false;
	float HeadingDegrees = 0.0f;

	/** World time the next AGAT ambient action is due; 0 re-arms it. */
	float NextIdleActionTime = 0.0f;

	/**
	 * Current terrain-alignment tilt, in the actor's local space, interpolated
	 * toward the ground normal every tick — see
	 * FSWGSkeletalAnimationPipeline::UpdateTerrainAlignment. Identity means
	 * "standing straight up relative to the capsule", which is the correct
	 * resting value on flat ground and the fallback whenever the ground trace
	 * misses.
	 */
	FQuat TerrainAlignment = FQuat::Identity;

	/**
	 * Lowest joint height (component space) while this actor is upright,
	 * re-sampled every tick it spends in that posture. The zero point for
	 * grounding: the skeleton's joints sit inside the body, so the lowest
	 * *joint* is already some way above the sole of the foot, and that
	 * constant has to be subtracted out rather than mistaken for hover.
	 * Unset until the actor has been seen upright at least once.
	 */
	TOptional<float> BaselineLowestBoneZ;

	/** How far the mesh is currently pushed down to keep a lying/seated pose on the ground — see UpdateMeshPlacement. */
	float GroundingOffset = 0.0f;
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
	void Shutdown();
#if WITH_DEV_AUTOMATION_TESTS
	friend class FSWGSpaceTeardownTest;
	friend class FSWGSpaceSkeletalLifetimeTest;
	TFunction<void()> BeforeSkeletalBuildForTest;
	TFunction<void(bool)> AfterSkeletalBuildForTest;
#endif

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
	void TryApplyGeneratedAnimatedMesh(AActor& Actor, const TArray<FString>& MeshVirtualPaths, const TMap<FString, FString>& AnimationLatPaths, UMeshComponent* ProceduralMeshComponent, const TMap<FString, FLinearColor>* PaletteTintOverrides = nullptr, const TMap<FString, float>* MorphWeights = nullptr, const TMap<FString, int32>* TextureIndexOverrides = nullptr, TSharedPtr<TWeakObjectPtr<AActor>, ESPMode::ThreadSafe> LoadingOwner = nullptr);

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

	/**
	 * Plays a one-shot combat action on Actor's generated mesh, resuming the
	 * interrupted loop after — same mechanics as a posture transition.
	 *
	 * ActionName is a combat_manager.iff action ("attack_mid_center_0"), not a
	 * logical animation; it resolves through the actor's .ash FORM ACTS, with
	 * WeaponStateName selecting the weapon subtree.
	 *
	 * False, and no change, if the action doesn't resolve or the actor is
	 * mid-transition — interrupting one would strand it on a held pose.
	 */
	bool PlayCombatAction(AActor& Actor, const FString& ActionName, const FString& WeaponStateName);

	/**
	 * Plays any .ash action (combat swing, emote, hit reaction) on its retail
	 * layer: an MVAC's moving variant while the actor is moving, additive
	 * (useAddTrack) clips on the add track, masked clips on the bone mask the
	 * LAT gives them, everything else full body over the loop. OutTrace, when
	 * given, receives what it resolved to or why it did not play.
	 */
	bool PlayAction(AActor& Actor, const FString& ActionName, const FString& WeaponStateName, FString* OutTrace = nullptr);

	bool SetDatapadPose(AActor& Actor, bool bHold);

	/** The layered anim instance driving Actor's mesh, or null when it has no generated animation. */
	USWGCharacterAnimInstance* FindAnimInstance(const AActor& Actor) const;

	/** Debug description of Actor's loop (kind, blend space, clip set) for swg.Anim.Dump. */
	FString DescribePlayback(const AActor& Actor) const;

	/**
	 * Props, not creatures: resolves a .sat (flag, banner, decoration) to its skeleton, mesh parts and the clips of the
	 * root state's loop in its .ash. False when the .sat, skeleton or .lat can't be read; a .lat without that loop
	 * still succeeds with no clips. Cached per .sat.
	 */
	bool ResolveSatLoopSource(const FString& SatPath, FSWGSatLoopSource& OutSource);

	/**
	 * Gives Component the generated skeletal mesh of Source and loops ClipPath on it (bind pose when ClipPath is
	 * empty). Works on any USkeletalMeshComponent, no ACharacter or tick registration; the creature path is untouched.
	 * TextureIndexOverrides are a cdf's CSSI values. OnDone(true) fires on the game thread once the pose is playing.
	 */
	void PlayLoopOnComponent(USkeletalMeshComponent& Component, const FSWGSatLoopSource& Source, const FString& ClipPath, const TMap<FString, int32>& TextureIndexOverrides, TFunction<void(bool)> OnDone);

	/**
	 * The clip PlayCombatAction would use, without playing it — every hop of
	 * the resolution is written to OutTrace for the swg.DumpCombatAnim
	 * console command. Empty when the action doesn't resolve.
	 */
	FString ResolveCombatActionClip(AActor& Actor, const FString& ActionName, const FString& WeaponStateName, FString* OutTrace = nullptr);

private:
	/**
	 * Same cache-hit/queue/backpressure shape as RequestGeneratedSkeletalMesh,
	 * for one locomotion clip. TargetSkeleton is the USkeleton the playing
	 * USkeletalMeshComponent actually uses (may include merged face bones) —
	 * Skeleton is the plain joint list the animated tracks are built from.
	 */
	TFuture<UAnimSequence*> RequestLocomotionAnimSequence(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FString& ClipPath, const FSWGSkeletonData& Skeleton, USkeleton* TargetSkeleton);

	/**
	 * Builds (or fetches) the idle/walk/run sequences of one clip set and then
	 * the blend space over them, calling OnReady with the result — null if any
	 * of the three failed to decode. The three sequence builds run
	 * independently and asynchronously; OnReady fires once, on the game
	 * thread, after all of them land.
	 *
	 * Shared by the first-time attach in TryApplyGeneratedAnimatedMesh and by
	 * Tick's posture swap, which need the same work for different follow-ups.
	 */
	void RequestLocomotionBlendSpace(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FSWGSkeletonData& Skeleton, USkeleton* TargetSkeleton, const FSWGLocomotionClipSet& ClipSet, float WalkSpeed, float RunSpeed, TFunction<void(UBlendSpace*)> OnReady);

	/**
	 * Loads and caches one species' .lat plus the .ash it names, keyed by LAT
	 * path. Null if either file is missing or unparseable. The returned
	 * pointer stays valid for the pipeline's lifetime (the map holds shared
	 * pointers, so growth doesn't invalidate it).
	 */
	const FSWGLocomotionSource* GetOrLoadLocomotionSource(const FString& LatPath);

	/**
	 * The speed one .ans was authored to travel at, in UE units/sec — its LOCT
	 * ground distance over its duration — cached per clip path. 0 for a clip
	 * with no LOCT (authored in place) or one that fails to decode.
	 *
	 * Separate from RequestLocomotionAnimSequence, which returns early on a
	 * generated-asset cache hit without decoding the source clip; the blend
	 * space needs this whether or not the sequence was rebuilt this session.
	 */
	float GetAuthoredClipSpeed(const FString& ClipPath);

	/** Re-resolves each playing animation's loop against its actor's current posture/states and swaps the blend space where it changed. Called from Tick. */
	void UpdatePostureDrivenAnimations();

	/** Puts BlendSpace on the loop track of MeshComponent's layered anim instance (creating it on first use) and records it against that component's playing-animation entry. */
	void BeginLoopPlayback(USkeletalMeshComponent& MeshComponent, UBlendSpace& BlendSpace, const FSWGLocomotionClipSet& ClipSet);

	/** Feeds speed, steering, look-at, aim, rider lean and ambient actions into each playing record's anim instance. Called from Tick. */
	void UpdateLayerInputs(float DeltaTime);

	/** The steering of the blend space a clip set resolves to. */
	static ESWGLoopKind DetermineLoopKind(const FSWGLocomotionClipSet& ClipSet);

	/** Mask asset path -> its joint names, read once (empty array remembers a failure). */
	const TArray<FName>& GetMaskBones(const FString& MaskPath);

	/**
	 * Requests every clip path (empty ones skipped), calling OnReady once on
	 * the game thread with sequences in the same order — null entries for
	 * clips that failed to decode.
	 */
	void RequestSequences(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FSWGSkeletonData& Skeleton, USkeleton* TargetSkeleton, const TArray<FString>& ClipPaths, TFunction<void(const TArray<UAnimSequence*>&)> OnReady);

	/** Builds (or loads) a 2D blend space from explicit samples — the turn-in-place and strafe variants. Axis 0 is speed, axis 1 per ESWGLoopKind. */
	UBlendSpace* GetOrBuildSteeredBlendSpace(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const TArray<FString>& SamplePaths, const TArray<UAnimSequence*>& Sequences, const TArray<FVector2D>& Positions, float MaxSpeed, float MinSteer, float MaxSteer, const FString& Salt, USkeleton* TargetSkeleton);

	/**
	 * Tilts each playing animation's mesh to follow the ground it's standing
	 * on, so a prone or sitting creature lies along a slope instead of
	 * intersecting it on one side and floating on the other.
	 *
	 * The tilt is applied to the mesh component rather than the actor because
	 * UCharacterMovementComponent requires its capsule to stay upright —
	 * rotating the actor would break ground checks and stepping.
	 *
	 * Also keeps the pose *on* that ground. The mesh hangs at
	 * -CapsuleHalfHeight so a standing pose's feet meet the capsule bottom,
	 * but a posture that lies the body down does it by rotating about the
	 * skeleton's root joint — which sits near the hips, not the feet — so the
	 * body swings up and hovers. The capsule can't simply be shrunk to
	 * compensate: it stays centred on the actor, so that moves the pivot
	 * without changing where the body ends up relative to it.
	 */
	void UpdateMeshPlacement(float DeltaTime);

	/**
	 * Loads the once-built UBlendSpace for this skeleton+mesh-parts+locomotion-clips
	 * combination, or builds and saves it if missing and this is an
	 * editor/PIE build: a single horizontal-speed axis with idle/walk/run
	 * samples at 0/WalkSpeed/RunSpeed. Synchronous — building a UBlendSpace
	 * is cheap UObject/package work only, no CPU-heavy pass worth threading.
	 */
	UBlendSpace* GetOrBuildLocomotionBlendSpace(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FSWGLocomotionClipSet& ClipSet, UAnimSequence* IdleSequence, UAnimSequence* WalkSequence, UAnimSequence* RunSequence, float WalkSpeed, float RunSpeed, USkeleton* TargetSkeleton);

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

	/**
	 * Mixed into every generated asset's package-name hash. The saved .uasset
	 * on disk *is* the cache, and its name derives from source paths only, so
	 * without this a decoder or importer fix keeps loading assets built by the
	 * superseded code and appears to do nothing. Bump it whenever a change
	 * alters what a given source file should produce: every generated asset is
	 * renamed and rebuilt.
	 */
	static constexpr uint32 GeneratedAssetVersion = 7;

	/** How far below the capsule's feet the ground trace reaches — enough to keep contact over small steps and terrain tessellation without finding the floor below a bridge. */
	static constexpr float TerrainAlignmentTraceDepth = 100.0f;
	/** Largest lean the terrain can induce. Beyond this the mesh keeps the clamped tilt rather than matching the slope outright. */
	static constexpr float MaxTerrainAlignmentAngleRadians = 0.6109f; // 35 degrees
	/** Higher converges on the ground normal faster; low enough that walking across tessellated terrain doesn't twitch. */
	static constexpr float TerrainAlignmentInterpSpeed = 8.0f;
	/** Ceiling on the grounding drop — a humanoid never needs more than about a hip's height, and this stops one bad pose from burying the mesh. */
	static constexpr float MaxGroundingDrop = 150.0f;

	/** Above this many already-finished-but-not-yet-finalized builds, the
	 *  worker pump stops pulling new requests until Tick catches up. */
	static constexpr int32 MaxPendingFinalize = 8;
	/** Finalized (NewObject/package/asset-registry work) per queue per Tick —
	 *  bounds how much game-thread work a single frame can absorb even if a
	 *  large backlog just finished on the worker side. */
	static constexpr int32 MaxFinalizePerTick = 2;

	bool bSlotHardpointsLoaded = false;
	TMap<FString, FString> SlotHardpoints;

	/** LAT path -> that species' parsed .lat + .ash, see GetOrLoadLocomotionSource. */
	TMap<FString, TSharedPtr<FSWGLocomotionSource>> LocomotionSources;

	/** .sat path -> its resolved prop animation source; null remembers a failure. See ResolveSatLoopSource. */
	TMap<FString, TSharedPtr<const FSWGSatLoopSource>> SatLoopSources;

	/** Mask asset path -> joint names, see GetMaskBones. */
	TMap<FString, TArray<FName>> MaskBoneCache;

	/** Clip path -> the speed it was authored to travel at, in UE units/sec. See GetAuthoredClipSpeed. */
	TMap<FString, float> AuthoredClipSpeeds;

	/** Actors whose blend space's speed input needs updating every tick —
	 *  see TryApplyGeneratedAnimatedMesh and Tick. */
	TArray<FSWGPlayingAnimation> PlayingAnimations;
};
