#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGSkeletonReader.h"
#include "TRE/SWGMeshReader.h"

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

/**
 * Owns the async skeletal-mesh + locomotion-animation generation pipeline —
 * split out of USWGMeshGeneratorSubsystem (which had grown past 3000 lines
 * across unrelated responsibilities: request dispatch, shader/texture/
 * customization resolution, static mesh building, and this) as the first
 * Option-B extraction. This piece was picked first because it's the newest
 * code, self-contained behind a small public surface (four methods), and
 * best understood while still fresh.
 *
 * Needs privileged access to some of the owning subsystem's private state
 * (TreSubsystem, MeshUtilities, GetOrBuildObjectMaterial) that wasn't worth
 * promoting to a public API just for this — see USWGMeshGeneratorSubsystem's
 * `friend class FSWGSkeletalAnimationPipeline;` declaration. Owned via
 * TUniquePtr by the subsystem, constructed in Initialize() once TreSubsystem/
 * MeshUtilities are set, after Collection.InitializeDependency has run.
 */
class SWGEMUCLIENT_API FSWGSkeletalAnimationPipeline
{
public:
	explicit FSWGSkeletalAnimationPipeline(USWGMeshGeneratorSubsystem& InOwner);

	/** Feeds each actor's live horizontal speed into its blend space every
	 *  tick — the blend space itself owns sample selection, blending, and
	 *  (via AxisToScaleAnimation, set in GetOrBuildLocomotionBlendSpace)
	 *  playback-rate scaling; see TryApplyGeneratedAnimatedMesh. Called from
	 *  USWGMeshGeneratorSubsystem::Tick. */
	void Tick(float DeltaTime);

	/**
	 * Generic per-species resolution: works for any ACharacter whose resolved
	 * mesh/skeleton data yields a SKTM skeleton reference and a usable LATX
	 * locomotion LAT. Swaps in the generated skeletal mesh (see
	 * GetOrBuildGeneratedSkeletalMeshAsync) directly on Character->GetMesh()
	 * (no longer kept hidden), hiding ProceduralMeshComponent instead, and
	 * plays a generated UBlendSpace (see GetOrBuildLocomotionBlendSpace) via
	 * UAnimSingleNodeInstance. No-op for anything without a skeleton
	 * (non-animated actors) or whose generated assets aren't available.
	 *
	 * Dispatches GetOrBuildGeneratedSkeletalMeshAsync and finishes the rest
	 * of the work (anim sequences, blend space, attach) in its completion
	 * callback — not synchronous, so no meaningful bool to return.
	 */
	void TryApplyGeneratedAnimatedMesh(AActor& Actor, const TArray<FString>& MeshVirtualPaths, const TMap<FString, FString>& AnimationLatPaths, UMeshComponent* ProceduralMeshComponent, const TMap<FString, FLinearColor>* PaletteTintOverrides = nullptr, const TMap<FString, float>* MorphWeights = nullptr, const TMap<FString, int32>* TextureIndexOverrides = nullptr);

private:
	/**
	 * Loads the once-built USkeletalMesh for this skeleton+mesh-parts
	 * combination (package name hashed from SkeletonPath + MeshVirtualPaths,
	 * under /Game/SWGEmu/Generated/), or builds and saves it via
	 * FSWGSkeletalMeshImporter if it doesn't exist yet and this is an
	 * editor/PIE build (see that class's WITH_EDITOR comment — there's no
	 * packaged-build-safe way to construct a real skinned mesh). The saved
	 * .uasset on disk *is* the cache — a later LoadObject call is the
	 * cache-hit path. If MeshVirtualPaths includes a "*_head*" part, also
	 * looks for a matching "<prefix>_face.skt" skeleton and merges it under
	 * the "head" joint for the mesh build only — the caller's own Skeleton
	 * (used to build locomotion animations) is left untouched since
	 * locomotion clips never animate face bones.
	 *
	 * Cache-hit (LoadObject) is checked synchronously before returning, since
	 * that's a game-thread-only asset op anyway — OnComplete fires
	 * immediately (still on the game thread, not deferred) in that case. On
	 * a cache miss, the actual parse+build work (FSWGSkeletalMeshImporter::
	 * BuildSkeletalMeshData) runs on a worker thread (Async(ThreadPool)),
	 * and OnComplete fires later, back on the game thread, with the
	 * finalized mesh (or nullptr on any resolve/parse/build failure).
	 */
	void GetOrBuildGeneratedSkeletalMeshAsync(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FSWGSkeletonData& Skeleton, TFunction<void(USkeletalMesh*)> OnComplete);

	/**
	 * Loads the once-built UAnimSequence for this skeleton+mesh-parts+clip
	 * combination (package name hashed from SkeletonPath + MeshVirtualPaths +
	 * ClipPath, under /Game/SWGEmu/Generated/ — MeshVirtualPaths is included
	 * because it's also part of GetOrBuildGeneratedSkeletalMeshAsync's own
	 * hash: two species can share one SkeletonPath's source .skt data but
	 * still get two distinct generated USkeleton objects, one per generated
	 * mesh, so an anim sequence built against the wrong one would be
	 * silently incompatible with whichever mesh is actually on screen).
	 * TargetSkeleton is the USkeleton the playing USkeletalMeshComponent
	 * actually uses (GeneratedMesh->GetSkeleton(), which may include merged
	 * face bones) — Skeleton is the plain joint list used to build the
	 * animated tracks themselves.
	 *
	 * Same cache-hit/worker-build/finalize shape as
	 * GetOrBuildGeneratedSkeletalMeshAsync: LoadObject cache check happens
	 * synchronously here; on a miss, FSWGAnimationImporter::
	 * BuildAnimSequenceData (the dense-track SLERP/LERP math) runs on a
	 * worker thread, and FinalizeAnimSequence (IAnimationDataController
	 * work — must be game thread) runs in OnComplete's continuation.
	 */
	void GetOrBuildLocomotionAnimSequenceAsync(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FString& ClipPath, const FSWGSkeletonData& Skeleton, USkeleton* TargetSkeleton, TFunction<void(UAnimSequence*)> OnComplete);

	/**
	 * Loads the once-built UBlendSpace for this skeleton+mesh-parts+locomotion-clips
	 * combination (package name hashed the same way as
	 * GetOrBuildLocomotionAnimSequenceAsync, for the same reason — see its
	 * comment), or builds and saves it if missing and this is an editor/PIE
	 * build: a single horizontal-speed axis with idle/walk/run samples at
	 * 0/WalkSpeed/RunSpeed. AxisToScaleAnimation is set to that axis, so the
	 * engine scales each sample's playback rate by how far the live blend
	 * input is from its own sample speed — no manual play-rate bookkeeping
	 * needed at runtime.
	 */
	UBlendSpace* GetOrBuildLocomotionBlendSpace(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const TArray<FString>& LocomotionPaths, UAnimSequence* IdleSequence, UAnimSequence* WalkSequence, UAnimSequence* RunSequence, float WalkSpeed, float RunSpeed, USkeleton* TargetSkeleton);

	/** Slot name -> hardpoint socket name, parsed once on first use from
	 *  abstract/slot/slot_definition/slot_definitions.iff — a small,
	 *  session-global reference table shared by every generated skeletal
	 *  mesh, not per-mesh data. Owned here rather than on the subsystem
	 *  since GetOrBuildGeneratedSkeletalMeshAsync is its only consumer. */
	const TMap<FString, FString>& GetSlotHardpoints();

	/** Privileged access to TreSubsystem/MeshUtilities/GetOrBuildObjectMaterial
	 *  — see this class's own header comment for why this isn't a narrower
	 *  interface. Never null after construction (constructed by the
	 *  subsystem itself, referencing itself). */
	USWGMeshGeneratorSubsystem& Owner;

	// Simplest possible backpressure for GetOrBuildGeneratedSkeletalMeshAsync
	// — refuses (OnComplete(nullptr)) rather than queues once the cap is
	// hit, so a burst of requests (e.g. a crowded zone entry) can't dispatch
	// unbounded concurrent worker tasks and thrash disk I/O. Callers already
	// tolerate a null result (falls back to the procedural bind-pose mesh),
	// and the request will naturally be retried on the actor's next
	// mesh-relevant event.
	int32 InFlightSkeletalMeshBuilds = 0;
	int32 MaxInFlightSkeletalMeshBuilds = 3;

	// Same backpressure idea as InFlightSkeletalMeshBuilds/
	// MaxInFlightSkeletalMeshBuilds, for GetOrBuildLocomotionAnimSequenceAsync.
	int32 InFlightAnimSequenceBuilds = 0;
	int32 MaxInFlightAnimSequenceBuilds = 6;

	bool bSlotHardpointsLoaded = false;
	TMap<FString, FString> SlotHardpoints;

	/** Actors whose blend space's speed input needs updating every tick —
	 *  see TryApplyGeneratedAnimatedMesh and Tick. */
	TArray<FSWGPlayingAnimation> PlayingAnimations;
};
