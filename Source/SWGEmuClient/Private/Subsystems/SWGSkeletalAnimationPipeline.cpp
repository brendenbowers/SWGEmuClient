#include "Subsystems/SWGSkeletalAnimationPipeline.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGAnimationReader.h"
#include "TRE/SWGSlotDefinitionReader.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SWGMovementComponent.h"
#include "Components/SWGCombatStateComponent.h"
#include "Common/SWGLocomotionResolver.h"
#include "TRE/SWGAshReader.h"
#include "TRE/SWGLatReader.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MeshUtilities.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"

namespace
{
	// "appearance/mesh/wke_m_head_l0.mgn" -> "appearance/skeleton/wke_m_face.skt".
	// Simply skipped if the file doesn't exist for a given species.
	FString DeriveFaceSkeletonPath(const TArray<FString>& MeshVirtualPaths)
	{
		for (const FString& MeshPath : MeshVirtualPaths)
		{
			const FString BaseName = FPaths::GetBaseFilename(MeshPath);
			const int32 HeadIndex = BaseName.Find(TEXT("_head"), ESearchCase::IgnoreCase);
			if (HeadIndex == INDEX_NONE)
			{
				continue;
			}
			return FString::Printf(TEXT("appearance/skeleton/%s_face.skt"), *BaseName.Left(HeadIndex));
		}
		return FString();
	}

	/**
	 * The posture and states the animation side should be showing for this
	 * actor. Read off USWGCombatStateComponent rather than passed in, because
	 * the pipeline re-checks them every tick — see UpdatePostureDrivenAnimations.
	 */
	void ReadPostureAndStates(const AActor& Actor, ESWGPosture& OutPosture, int64& OutStateBitmask)
	{
		OutPosture = ESWGPosture::Upright;
		OutStateBitmask = 0;

		if (const USWGCombatStateComponent* CombatState = Actor.FindComponentByClass<USWGCombatStateComponent>())
		{
			OutPosture = CombatState->GetPosture();
			OutStateBitmask = CombatState->StateBitmask;
		}
	}

	/**
	 * Speeds the blend space's samples are placed at. These are the *posture-
	 * scaled* walk/run speeds, not the raw CREO ones: a prone creature's
	 * observed velocity tops out at a quarter of its run speed, so placing the
	 * crawl sample at the unscaled run speed would leave it permanently
	 * blended toward the idle end.
	 */
	void ReadBlendSpeeds(const ACharacter& Character, float& OutWalkSpeed, float& OutRunSpeed)
	{
		const USWGMovementComponent* Movement = Cast<USWGMovementComponent>(Character.GetCharacterMovement());
		OutWalkSpeed = Movement ? Movement->GetPostureWalkSpeed() : 0.0f;
		OutRunSpeed = Movement ? Movement->GetPostureRunSpeed() : 0.0f;

		if (OutWalkSpeed <= KINDA_SMALL_NUMBER)
		{
			OutWalkSpeed = 155.0f;
		}
		if (OutRunSpeed <= OutWalkSpeed)
		{
			OutRunSpeed = FMath::Max(OutWalkSpeed * 2.0f, Movement ? Movement->MaxWalkSpeed : 310.0f);
		}
	}
}

FSWGSkeletalAnimationPipeline::FSWGSkeletalAnimationPipeline(USWGMeshGeneratorSubsystem& InOwner)
	: Owner(InOwner)
{
}

FSWGSkeletalAnimationPipeline::~FSWGSkeletalAnimationPipeline()
{
	// Signal both pump loops to exit, then wait for them to actually notice
	// and return before this object finishes destructing — a pump mid-loop
	// still dereferences *this* (Owner, the queues, the atomics themselves),
	// so this has to be a real wait, not fire-and-forget. Rare/teardown-only
	// path (subsystem Deinitialize / game instance shutdown), so a short
	// spin-sleep is fine — this is not a hot path.
	bShuttingDown = true;
	while (NumActiveWorkerTasks.load() > 0)
	{
		FPlatformProcess::Sleep(0.001f);
	}
}

void FSWGSkeletalAnimationPipeline::Tick(float DeltaTime)
{
	// Feeds each actor's live horizontal speed into its blend space every
	// tick — the blend space itself owns sample selection, blending, and
	// (via AxisToScaleAnimation, set in GetOrBuildLocomotionBlendSpace)
	// playback-rate scaling; see TryApplyGeneratedAnimatedMesh.
	for (int32 i = PlayingAnimations.Num() - 1; i >= 0; --i)
	{
		FSWGPlayingAnimation& Playing = PlayingAnimations[i];
		USkeletalMeshComponent* MeshComponent = Playing.MeshComponent.Get();
		UAnimSingleNodeInstance* AnimInstance = Playing.AnimInstance.Get();
		if (!MeshComponent || !AnimInstance)
		{
			PlayingAnimations.RemoveAtSwap(i);
			continue;
		}

		// While a posture-change transition is playing the instance holds a
		// plain sequence, not a blend space, so there's no speed axis to feed.
		if (Playing.PendingBlendSpace.IsValid())
		{
			continue;
		}

		const ACharacter* Character = Cast<ACharacter>(MeshComponent->GetOwner());
		float HorizontalSpeed = Character ? Character->GetVelocity().Size2D() : 0.0f;
		if (const USWGMovementComponent* Movement = Character ? Cast<USWGMovementComponent>(Character->GetCharacterMovement()) : nullptr)
		{
			if (Movement->LastNetworkUpdateTime > 0.0f && MeshComponent->GetWorld())
			{
				const float TimeSinceUpdate = MeshComponent->GetWorld()->GetTimeSeconds() - Movement->LastNetworkUpdateTime;
				if (TimeSinceUpdate > 0.5f)
				{
					HorizontalSpeed = 0.0f;
				}
			}
		}
		AnimInstance->SetBlendSpacePosition(FVector(HorizontalSpeed, 0.0f, 0.0f));
	}

	UpdatePostureDrivenAnimations();
	UpdatePendingTransitions();
	UpdateMeshPlacement(DeltaTime);

	// Drain a budgeted number of finished builds (game-thread finalize work)
	// before re-arming the pumps, so a pump that was paused on backpressure
	// gets a fresh read of the (now smaller) finalize backlog.
	// TODO: switch to measuring the time spent finalizing and stop when a budgeted frame-time is exceeded, instead of a fixed count — some builds are much heavier than others.
	DrainCompletedSkeletalMeshBuilds(MaxFinalizePerTick);
	DrainCompletedAnimSequenceBuilds(MaxFinalizePerTick);

	if (!bSkeletalMeshWorkerActive.load() && !PendingSkeletalMeshRequests.IsEmpty() && PendingSkeletalMeshFinalizeCount.load() < MaxPendingFinalize)
	{
		bSkeletalMeshWorkerActive = true;
		++NumActiveWorkerTasks;
		Async(EAsyncExecution::ThreadPool, [this]() { PumpSkeletalMeshWork(); });
	}
	if (!bAnimSequenceWorkerActive.load() && !PendingAnimSequenceRequests.IsEmpty() && PendingAnimSequenceFinalizeCount.load() < MaxPendingFinalize)
	{
		bAnimSequenceWorkerActive = true;
		++NumActiveWorkerTasks;
		Async(EAsyncExecution::ThreadPool, [this]() { PumpAnimSequenceWork(); });
	}
}

void FSWGSkeletalAnimationPipeline::UpdateMeshPlacement(float DeltaTime)
{
	for (FSWGPlayingAnimation& Playing : PlayingAnimations)
	{
		USkeletalMeshComponent* MeshComponent = Playing.MeshComponent.Get();
		ACharacter* Character = MeshComponent ? Cast<ACharacter>(MeshComponent->GetOwner()) : nullptr;
		UWorld* World = MeshComponent ? MeshComponent->GetWorld() : nullptr;
		if (!Character || !World)
		{
			continue;
		}

		// Trace from inside the capsule downward past its feet. Starting at
		// the actor origin rather than above it keeps the trace from catching
		// a ceiling or an overhanging mesh in an interior.
		const FVector Start = Character->GetActorLocation();
		float TraceDown = 200.0f;
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			TraceDown = Capsule->GetScaledCapsuleHalfHeight() + TerrainAlignmentTraceDepth;
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(SWGTerrainAlignment), /*bTraceComplex=*/true, Character);
		FHitResult Hit;
		FQuat TargetAlignment = FQuat::Identity;

		if (World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0f, 0.0f, TraceDown), ECC_WorldStatic, Params)
			&& Hit.ImpactNormal.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			// The normal is world space but the tilt is applied to a component
			// whose parent (the capsule) carries the actor's yaw, so it has to
			// be expressed in the actor's frame or the character would lean the
			// wrong way as it turned.
			const FVector LocalNormal = Character->GetActorTransform().InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();

			FQuat Alignment = FQuat::FindBetweenNormals(FVector::UpVector, LocalNormal);

			// Full alignment reads badly on anything steep — a biped ends up
			// visibly leaning out of the hill. Clamp to a believable lean and
			// let the rest of the slope go unmatched, which is what the retail
			// client's creatures do too.
			FVector Axis;
			float Angle = 0.0f;
			Alignment.ToAxisAndAngle(Axis, Angle);
			if (Angle > MaxTerrainAlignmentAngleRadians)
			{
				Alignment = FQuat(Axis, MaxTerrainAlignmentAngleRadians);
			}
			TargetAlignment = Alignment;
		}

		// Interpolated rather than snapped: the trace result jumps between
		// triangles as the creature walks, and applying that directly makes
		// the mesh visibly twitch on tessellated terrain.
		Playing.TerrainAlignment = FQuat::Slerp(Playing.TerrainAlignment, TargetAlignment,
			FMath::Clamp(DeltaTime * TerrainAlignmentInterpSpeed, 0.0f, 1.0f)).GetNormalized();

		// SWG's forward axis offset first, then the tilt — see the same yaw
		// correction applied at attach time in TryApplyGeneratedAnimatedMesh.
		const FQuat BaseRotation = FRotator(0.0f, SWGCharacterMeshYaw, 0.0f).Quaternion();
		MeshComponent->SetRelativeRotation(BaseRotation * Playing.TerrainAlignment);

		// Grounding, measured from the joints rather than the component's
		// bounds — those are reference-pose bounds (their extents are
		// identical across standing, kneeling, prone and sitting) and say
		// nothing about where the current pose sits.
		const TArray<FTransform>& BoneTransforms = MeshComponent->GetComponentSpaceTransforms();
		if (BoneTransforms.Num() == 0)
		{
			continue;
		}

		float LowestBoneZ = TNumericLimits<float>::Max();
		for (const FTransform& BoneTransform : BoneTransforms)
		{
			LowestBoneZ = FMath::Min(LowestBoneZ, (float)BoneTransform.GetTranslation().Z);
		}
		if (!FMath::IsFinite(LowestBoneZ))
		{
			continue;
		}

		// The first evaluated pose is the standing one, and its lowest joint
		// is the zero point — joints sit inside the body, so an ankle is
		// already well above the sole it rests on. Only the *change* from
		// there is hover worth correcting.
		if (!Playing.BaselineLowestBoneZ.IsSet())
		{
			Playing.BaselineLowestBoneZ = LowestBoneZ;
		}

		// Clamped at zero so this can only ever push the mesh down onto the
		// ground, never lift it: a pose that legitimately reaches below the
		// standing baseline (a deep crouch, a stumble) is left alone.
		const float TargetDrop = FMath::Clamp(LowestBoneZ - *Playing.BaselineLowestBoneZ, 0.0f, MaxGroundingDrop);
		Playing.GroundingOffset = FMath::FInterpTo(Playing.GroundingOffset, TargetDrop, DeltaTime, TerrainAlignmentInterpSpeed);

		const float CapsuleHalfHeight = Character->GetCapsuleComponent()
			? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: 0.0f;
		MeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -CapsuleHalfHeight - Playing.GroundingOffset));
	}
}

void FSWGSkeletalAnimationPipeline::UpdatePostureDrivenAnimations()
{
	for (int32 i = 0; i < PlayingAnimations.Num(); ++i)
	{
		FSWGPlayingAnimation& Playing = PlayingAnimations[i];
		USkeletalMeshComponent* MeshComponent = Playing.MeshComponent.Get();
		ACharacter* Character = MeshComponent ? Cast<ACharacter>(MeshComponent->GetOwner()) : nullptr;
		USkeleton* TargetSkeleton = Playing.TargetSkeleton.Get();
		if (!Character || !TargetSkeleton || Playing.bSwapInFlight)
		{
			continue;
		}

		ESWGPosture Posture = ESWGPosture::Upright;
		int64 StateBitmask = 0;
		ReadPostureAndStates(*Character, Posture, StateBitmask);
		if (Posture == Playing.Posture && StateBitmask == Playing.StateBitmask)
		{
			continue;
		}

		// Recorded before the resolve so a posture whose loop is unchanged (or
		// whose clips fail to resolve) doesn't re-walk the hierarchy every
		// single tick from here on.
		const ESWGPosture PreviousPosture = Playing.Posture;
		Playing.Posture = Posture;
		Playing.StateBitmask = StateBitmask;

		const FSWGLocomotionSource* Source = GetOrLoadLocomotionSource(Playing.LatPath);
		if (!Source)
		{
			continue;
		}

		FSWGLocomotionClipSet ClipSet;
		if (!SWGLocomotion::ResolveClipSet(Source->Hierarchy, Source->Lat, Posture, StateBitmask, ClipSet))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: %s has no usable clips for posture %d — keeping '%s'"),
				*Character->GetName(), (int32)Posture, *Playing.ClipSet.IdleLoopName);
			continue;
		}

		if (ClipSet == Playing.ClipSet)
		{
			continue;
		}

		float WalkSpeed = 0.0f;
		float RunSpeed = 0.0f;
		ReadBlendSpeeds(*Character, WalkSpeed, RunSpeed);

		Playing.bSwapInFlight = true;

		// Abandon any transition still queued from an earlier change —
		// changing posture again mid-transition would otherwise let the
		// superseded loop start once its timer elapsed, showing the wrong
		// posture until the new blend space landed.
		Playing.PendingBlendSpace.Reset();

		// The clip the .ash authors for this particular posture change, if it
		// has one — see UpdatePendingTransitions for how it's sequenced ahead
		// of the destination loop.
		const FString TransitionClipPath = SWGLocomotion::ResolveTransitionClip(
			Source->Hierarchy, Source->Lat, PreviousPosture, Posture, StateBitmask);
		const FString SkeletonPathCopy = Playing.SkeletonPath;
		const TArray<FString> MeshPathsCopy = Playing.MeshVirtualPaths;
		const FSWGSkeletonData SkeletonCopy = Playing.Skeleton;

		// The completion re-finds the record by mesh component rather than
		// capturing &Playing: PlayingAnimations is a TArray that Tick's own
		// RemoveAtSwap can reorder while the sequence builds are in flight.
		TWeakObjectPtr<USkeletalMeshComponent> MeshComponentWeak(MeshComponent);
		RequestLocomotionBlendSpace(Playing.SkeletonPath, Playing.MeshVirtualPaths, Playing.Skeleton, TargetSkeleton, ClipSet, WalkSpeed, RunSpeed,
			[this, MeshComponentWeak, ClipSet, TransitionClipPath, SkeletonPathCopy, MeshPathsCopy, SkeletonCopy, TargetSkeleton](UBlendSpace* BlendSpace)
			{
				FSWGPlayingAnimation* Record = PlayingAnimations.FindByPredicate(
					[&MeshComponentWeak](const FSWGPlayingAnimation& Candidate) { return Candidate.MeshComponent == MeshComponentWeak; });
				if (!Record)
				{
					return;
				}
				Record->bSwapInFlight = false;

				USkeletalMeshComponent* MeshComponent = MeshComponentWeak.Get();
				if (!BlendSpace || !MeshComponent)
				{
					return;
				}

				// No authored transition for this posture pair (most pairs have
				// none) — go straight to the destination loop.
				if (TransitionClipPath.IsEmpty())
				{
					BeginLoopPlayback(*MeshComponent, *BlendSpace, ClipSet);
					return;
				}

				// Otherwise play the transition clip once, unlooped, and let
				// UpdatePendingTransitions start the loop when it finishes.
				RequestLocomotionAnimSequence(SkeletonPathCopy, MeshPathsCopy, TransitionClipPath, SkeletonCopy, TargetSkeleton)
					.Next([this, MeshComponentWeak, ClipSet, BlendSpace](UAnimSequence* TransitionSequence)
					{
						FSWGPlayingAnimation* Record = PlayingAnimations.FindByPredicate(
							[&MeshComponentWeak](const FSWGPlayingAnimation& Candidate) { return Candidate.MeshComponent == MeshComponentWeak; });
						USkeletalMeshComponent* MeshComponent = MeshComponentWeak.Get();
						if (!Record || !MeshComponent || !IsValid(BlendSpace))
						{
							return;
						}

						// The clip failed to decode — the destination pose is
						// still correct, just reached without the motion.
						if (!TransitionSequence || TransitionSequence->GetPlayLength() <= 0.0f)
						{
							BeginLoopPlayback(*MeshComponent, *BlendSpace, ClipSet);
							return;
						}

						MeshComponent->PlayAnimation(TransitionSequence, false);
						if (UAnimSingleNodeInstance* AnimInstance = Cast<UAnimSingleNodeInstance>(MeshComponent->GetAnimInstance()))
						{
							Record->AnimInstance = AnimInstance;
						}
						Record->ClipSet = ClipSet;
						Record->PendingBlendSpace = BlendSpace;
						Record->PendingBlendSpaceStartTime = MeshComponent->GetWorld()
							? MeshComponent->GetWorld()->GetTimeSeconds() + TransitionSequence->GetPlayLength()
							: 0.0f;
					});
			});
	}
}

void FSWGSkeletalAnimationPipeline::BeginLoopPlayback(USkeletalMeshComponent& MeshComponent, UBlendSpace& BlendSpace, const FSWGLocomotionClipSet& ClipSet)
{
	MeshComponent.PlayAnimation(&BlendSpace, true);

	FSWGPlayingAnimation* Record = PlayingAnimations.FindByPredicate(
		[&MeshComponent](const FSWGPlayingAnimation& Candidate) { return Candidate.MeshComponent.Get() == &MeshComponent; });
	if (!Record)
	{
		return;
	}

	Record->PendingBlendSpace.Reset();
	if (UAnimSingleNodeInstance* AnimInstance = Cast<UAnimSingleNodeInstance>(MeshComponent.GetAnimInstance()))
	{
		Record->AnimInstance = AnimInstance;
	}
	Record->ClipSet = ClipSet;
}

void FSWGSkeletalAnimationPipeline::UpdatePendingTransitions()
{
	for (FSWGPlayingAnimation& Playing : PlayingAnimations)
	{
		UBlendSpace* PendingBlendSpace = Playing.PendingBlendSpace.Get();
		USkeletalMeshComponent* MeshComponent = Playing.MeshComponent.Get();
		UWorld* World = MeshComponent ? MeshComponent->GetWorld() : nullptr;
		if (!PendingBlendSpace || !MeshComponent || !World)
		{
			continue;
		}

		// Driven off elapsed world time rather than the anim instance's own
		// position: a non-looping single-node animation simply holds its last
		// frame when it ends, so there's no completion event to hook, and
		// polling GetCurrentTime would need the same clock comparison anyway.
		if (World->GetTimeSeconds() >= Playing.PendingBlendSpaceStartTime)
		{
			BeginLoopPlayback(*MeshComponent, *PendingBlendSpace, Playing.ClipSet);
		}
	}
}

const FSWGLocomotionSource* FSWGSkeletalAnimationPipeline::GetOrLoadLocomotionSource(const FString& LatPath)
{
	if (LatPath.IsEmpty() || !Owner.TreSubsystem)
	{
		return nullptr;
	}

	if (const TSharedPtr<FSWGLocomotionSource>* Existing = LocomotionSources.Find(LatPath))
	{
		return Existing->Get();
	}

	TSharedPtr<FSWGLocomotionSource> Source = MakeShared<FSWGLocomotionSource>();
	if (!FSWGLatReader::ReadLat(Owner.TreSubsystem->CreateIffReader(LatPath), Source->Lat))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to parse LAT '%s'"), *LatPath);
		// Cached as null so a species with a broken LAT isn't re-read every
		// tick by UpdatePostureDrivenAnimations.
		LocomotionSources.Add(LatPath, nullptr);
		return nullptr;
	}

	if (!FSWGAshReader::ReadHierarchy(Owner.TreSubsystem->CreateIffReader(Source->Lat.AshPath), Source->Hierarchy))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: LAT '%s' names ASH '%s', which failed to parse"), *LatPath, *Source->Lat.AshPath);
		LocomotionSources.Add(LatPath, nullptr);
		return nullptr;
	}

	return LocomotionSources.Add(LatPath, Source).Get();
}

void FSWGSkeletalAnimationPipeline::RequestLocomotionBlendSpace(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FSWGSkeletonData& Skeleton, USkeleton* TargetSkeleton, const FSWGLocomotionClipSet& ClipSet, float WalkSpeed, float RunSpeed, TFunction<void(UBlendSpace*)> OnReady)
{
	// Idle/Walk/Run each build independently and asynchronously — this join
	// runs OnReady exactly once, after all three land, regardless of which
	// order they actually complete in. All three futures resolve on the game
	// thread (see RequestLocomotionAnimSequence), so NumCompleted needs no
	// synchronization.
	struct FSWGLocomotionJoinState
	{
		UAnimSequence* IdleSequence = nullptr;
		UAnimSequence* WalkSequence = nullptr;
		UAnimSequence* RunSequence = nullptr;
		int32 NumCompleted = 0;
	};
	TSharedRef<FSWGLocomotionJoinState> JoinState = MakeShared<FSWGLocomotionJoinState>();

	auto OnOneAnimSequenceReady = [this, JoinState, SkeletonPath, MeshVirtualPaths, ClipSet, WalkSpeed, RunSpeed, TargetSkeleton, OnReady]()
	{
		if (JoinState->NumCompleted < 3)
		{
			return;
		}

		OnReady(GetOrBuildLocomotionBlendSpace(SkeletonPath, MeshVirtualPaths, ClipSet, JoinState->IdleSequence, JoinState->WalkSequence, JoinState->RunSequence, WalkSpeed, RunSpeed, TargetSkeleton));
	};

	RequestLocomotionAnimSequence(SkeletonPath, MeshVirtualPaths, ClipSet.IdlePath, Skeleton, TargetSkeleton)
		.Next([JoinState, OnOneAnimSequenceReady](UAnimSequence* Seq) { JoinState->IdleSequence = Seq; ++JoinState->NumCompleted; OnOneAnimSequenceReady(); });
	RequestLocomotionAnimSequence(SkeletonPath, MeshVirtualPaths, ClipSet.WalkPath, Skeleton, TargetSkeleton)
		.Next([JoinState, OnOneAnimSequenceReady](UAnimSequence* Seq) { JoinState->WalkSequence = Seq; ++JoinState->NumCompleted; OnOneAnimSequenceReady(); });
	RequestLocomotionAnimSequence(SkeletonPath, MeshVirtualPaths, ClipSet.RunPath, Skeleton, TargetSkeleton)
		.Next([JoinState, OnOneAnimSequenceReady](UAnimSequence* Seq) { JoinState->RunSequence = Seq; ++JoinState->NumCompleted; OnOneAnimSequenceReady(); });
}

const TMap<FString, FString>& FSWGSkeletalAnimationPipeline::GetSlotHardpoints()
{
	if (bSlotHardpointsLoaded)
	{
		return SlotHardpoints;
	}
	bSlotHardpointsLoaded = true;

	TArray<FSWGSlotDefinition> Definitions;
	if (!FSWGSlotDefinitionReader::Read(
		Owner.TreSubsystem->CreateIffReader(TEXT("abstract/slot/slot_definition/slot_definitions.iff")),
		Definitions))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to read slot definitions"));
		return SlotHardpoints;
	}

	for (const FSWGSlotDefinition& Definition : Definitions)
	{
		if (!Definition.Hardpoint.IsEmpty())
		{
			SlotHardpoints.Add(Definition.Name, Definition.Hardpoint);
		}
	}
	return SlotHardpoints;
}

TFuture<USkeletalMesh*> FSWGSkeletalAnimationPipeline::RequestGeneratedSkeletalMesh(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FSWGSkeletonData& Skeleton)
{
	const uint32 PathsHash = GetTypeHash(SkeletonPath) ^ GetTypeHash(MeshVirtualPaths) ^ GeneratedAssetVersion;
	const FString AssetName = FString::Printf(TEXT("SK_%u"), PathsHash);
	const FString PackagePath = TEXT("/Game/SWGEmu/Generated/") + AssetName;

	// The saved .uasset on disk is the cache — a hit here skips rebuilding
	// entirely. LoadObject is a game-thread-only asset op, so this check has
	// to happen here, synchronously, before anything is queued.
	if (USkeletalMesh* Existing = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName)))
	{
		TPromise<USkeletalMesh*> Promise;
		Promise.SetValue(Existing);
		return Promise.GetFuture();
	}

#if WITH_EDITOR
	TSharedPtr<TPromise<USkeletalMesh*>> Promise = MakeShared<TPromise<USkeletalMesh*>>();
	TFuture<USkeletalMesh*> Future = Promise->GetFuture();

	FSWGSkeletalMeshBuildRequest Request;
	Request.SkeletonPath = SkeletonPath;
	Request.MeshVirtualPaths = MeshVirtualPaths;
	Request.Skeleton = Skeleton;
	Request.PackagePath = PackagePath;
	// GetSlotHardpoints() mutates game-thread-only state on first use —
	// resolve it here, now, and hand the worker pump a plain copy.
	Request.SlotHardpoints = GetSlotHardpoints();
	Request.Promise = MoveTemp(Promise);
	PendingSkeletalMeshRequests.Enqueue(MoveTemp(Request));

	return Future;
#else
	UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: generated skeletal mesh '%s' isn't built yet and can't be built in a packaged build — leaving skeleton '%s' on its procedural bind-pose mesh"), *PackagePath, *SkeletonPath);
	TPromise<USkeletalMesh*> Promise;
	Promise.SetValue(nullptr);
	return Promise.GetFuture();
#endif
}

#if WITH_EDITOR
void FSWGSkeletalAnimationPipeline::PumpSkeletalMeshWork()
{
	// MeshUtilities is loaded once on the game thread in
	// USWGMeshGeneratorSubsystem::Initialize() and never unloaded during a
	// session — safe to read repeatedly from a worker thread.
	IMeshUtilities* MeshUtilitiesModule = Owner.MeshUtilities;
	check(MeshUtilitiesModule);

	for (;;)
	{
		if (bShuttingDown.load())
		{
			break;
		}
		if (PendingSkeletalMeshFinalizeCount.load() >= MaxPendingFinalize)
		{
			// Backpressure: the game thread hasn't kept up finalizing
			// already-completed builds. Stop pulling new work — Tick will
			// re-arm this pump once PendingSkeletalMeshFinalizeCount drops.
			break;
		}

		// IsEmpty() first just avoids a pointless default-construct-then-throw-
		// away of Request when there's nothing to pull — not required for
		// correctness anymore. (Promise used to be a plain TPromise here,
		// which was actually unsafe regardless of this check: see
		// FSWGSkeletalMeshBuildRequest::Promise's comment for why it's a
		// TSharedPtr now.)
		if (PendingSkeletalMeshRequests.IsEmpty())
		{
			break; // caught up — nothing left to do right now
		}
		FSWGSkeletalMeshBuildRequest Request;
		if (!PendingSkeletalMeshRequests.Dequeue(Request))
		{
			break;
		}

		FSWGSkeletalMeshBuildResult Result;
		Result.PackagePath = Request.PackagePath;
		Result.SkeletonPath = Request.SkeletonPath;
		Result.MeshVirtualPaths = Request.MeshVirtualPaths;
		Result.Promise = MoveTemp(Request.Promise);

		FSWGSkeletonData MeshBuildSkeleton = Request.Skeleton;
		const FString FaceSkeletonPath = DeriveFaceSkeletonPath(Request.MeshVirtualPaths);
		if (!FaceSkeletonPath.IsEmpty())
		{
			FSWGSkeletonData FaceSkeleton;
			if (FSWGSkeletonReader::ReadSkeleton(Owner.TreSubsystem->CreateIffReader(FaceSkeletonPath), FaceSkeleton))
			{
				const int32 HeadJointIndex = MeshBuildSkeleton.Joints.IndexOfByPredicate([](const FSWGSkeletonJoint& Joint) { return Joint.Name.Equals(TEXT("head"), ESearchCase::IgnoreCase); });
				if (HeadJointIndex != INDEX_NONE)
				{
					const int32 FaceBaseIndex = MeshBuildSkeleton.Joints.Num();
					for (FSWGSkeletonJoint Joint : FaceSkeleton.Joints)
					{
						Joint.ParentIndex = Joint.ParentIndex == INDEX_NONE ? HeadJointIndex : FaceBaseIndex + Joint.ParentIndex;
						MeshBuildSkeleton.Joints.Add(MoveTemp(Joint));
					}
				}
			}
		}

		TArray<FSWGMeshData> MeshParts;
		MeshParts.Reserve(Request.MeshVirtualPaths.Num());
		for (const FString& MeshPath : Request.MeshVirtualPaths)
		{
			FSWGMeshData PartData;
			if (FSWGMeshReader::ReadSkeletalMeshBindPose(Owner.TreSubsystem->CreateIffReader(MeshPath), PartData))
			{
				MeshParts.Add(MoveTemp(PartData));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to parse skeletal mesh part '%s' while building generated mesh for skeleton '%s'"), *MeshPath, *Request.SkeletonPath);
			}
		}

		if (MeshParts.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: no usable mesh parts to build a generated skeletal mesh for skeleton '%s'"), *Request.SkeletonPath);
			Result.bBuilt = false;
		}
		else
		{
			TArray<const FSWGMeshData*> MeshPartPtrs;
			MeshPartPtrs.Reserve(MeshParts.Num());
			for (const FSWGMeshData& Part : MeshParts)
			{
				MeshPartPtrs.Add(&Part);
			}

			// The actual expensive geometry-build pass — FMeshUtilities::
			// BuildSkeletalMesh underneath, confirmed thread-safe (see
			// mesh-threading-plan.html's engine-research table).
			Result.bBuilt = FSWGSkeletalMeshImporter::BuildSkeletalMeshData(*MeshUtilitiesModule, MeshBuildSkeleton, MeshPartPtrs, Request.SlotHardpoints, Request.PackagePath, Result.BuildData);
		}

		++PendingSkeletalMeshFinalizeCount;
		CompletedSkeletalMeshBuilds.Enqueue(MoveTemp(Result));
	}

	bSkeletalMeshWorkerActive = false;
	--NumActiveWorkerTasks;
}
#else
void FSWGSkeletalAnimationPipeline::PumpSkeletalMeshWork()
{
	bSkeletalMeshWorkerActive = false;
	--NumActiveWorkerTasks;
}
#endif

void FSWGSkeletalAnimationPipeline::DrainCompletedSkeletalMeshBuilds(int32 MaxToProcess)
{
	int32 Processed = 0;
	// IsEmpty() first just avoids a pointless default-construct-then-throw-
	// away of Result when there's nothing to drain — not required for
	// correctness (see FSWGSkeletalMeshBuildRequest::Promise's comment for
	// the actual TSharedPtr fix, and why a plain TPromise member here was
	// unsafe regardless of how carefully this call site checked Dequeue()).
	while (Processed < MaxToProcess && !CompletedSkeletalMeshBuilds.IsEmpty())
	{
		FSWGSkeletalMeshBuildResult Result;
		if (!CompletedSkeletalMeshBuilds.Dequeue(Result))
		{
			break;
		}

		--PendingSkeletalMeshFinalizeCount;
		++Processed;

		if (!Result.bBuilt)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to build generated skeletal mesh data '%s' for skeleton '%s'"), *Result.PackagePath, *Result.SkeletonPath);
			Result.Promise->SetValue(nullptr);
			continue;
		}

		// Grabbed before the MoveTemp below hands BuildData's guts off to
		// FinalizeSkeletalMesh — plain data, cheap to copy, needed further
		// down to attach USWGMeshOcclusionZoneData.
		TArray<TArray<FString>> OcclusionZoneNamesBySection = Result.BuildData.OcclusionZoneNamesBySection;

		// Everything from here on is UObject/package/asset-registry work —
		// must run on the game thread (see FSWGSkeletalMeshImporter::
		// FinalizeSkeletalMesh's own comment), which is exactly where Tick
		// (and therefore this function) always runs.
		USkeletalMesh* Mesh = FSWGSkeletalMeshImporter::FinalizeSkeletalMesh(MoveTemp(Result.BuildData), Result.PackagePath);
		if (!Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to finalize generated skeletal mesh '%s' for skeleton '%s'"), *Result.PackagePath, *Result.SkeletonPath);
			Result.Promise->SetValue(nullptr);
			continue;
		}

		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: built and cached generated skeletal mesh '%s' for skeleton '%s'"), *Result.PackagePath, *Result.SkeletonPath);

		// FinalizeSkeletalMesh already saved its package before returning,
		// so the user data has to be added and the package re-saved here —
		// otherwise it'd only live in memory for this session and be
		// missing again on the next cold LoadObject cache-hit.
		USWGMeshSourceUserData* SourceData = NewObject<USWGMeshSourceUserData>(Mesh);
		SourceData->DebugName = FString::Printf(TEXT("%s (skeleton %s)"), *FString::Join(Result.MeshVirtualPaths, TEXT(", ")), *Result.SkeletonPath);
		Mesh->AddAssetUserData(SourceData);

		// Only attach if at least one section actually has zone names — most
		// non-humanoid or non-clothing geometry has none at all (see
		// USWGMeshOcclusionZoneData's own comment).
		const bool bHasAnyOcclusionZones = OcclusionZoneNamesBySection.ContainsByPredicate(
			[](const TArray<FString>& Zones) { return !Zones.IsEmpty(); });
		if (bHasAnyOcclusionZones)
		{
			USWGMeshOcclusionZoneData* OcclusionData = NewObject<USWGMeshOcclusionZoneData>(Mesh);
			OcclusionData->ZoneNamesBySection.Reserve(OcclusionZoneNamesBySection.Num());
			for (TArray<FString>& Zones : OcclusionZoneNamesBySection)
			{
				FSWGMeshSectionOcclusionZones& Section = OcclusionData->ZoneNamesBySection.AddDefaulted_GetRef();
				Section.ZoneNames = MoveTemp(Zones);
			}
			Mesh->AddAssetUserData(OcclusionData);
		}

		if (UPackage* ResultPackage = Mesh->GetPackage())
		{
			ResultPackage->MarkPackageDirty();
			const FString FileName = FPackageName::LongPackageNameToFilename(ResultPackage->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			UPackage::SavePackage(ResultPackage, Mesh, *FileName, SaveArgs);
		}

		Result.Promise->SetValue(Mesh);
	}
}

TFuture<UAnimSequence*> FSWGSkeletalAnimationPipeline::RequestLocomotionAnimSequence(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FString& ClipPath, const FSWGSkeletonData& Skeleton, USkeleton* TargetSkeleton)
{
	const uint32 PathsHash = GetTypeHash(SkeletonPath) ^ GetTypeHash(MeshVirtualPaths) ^ GetTypeHash(ClipPath) ^ GeneratedAssetVersion;
	const FString AssetName = FString::Printf(TEXT("AS_%u"), PathsHash);
	const FString PackagePath = TEXT("/Game/SWGEmu/Generated/") + AssetName;

	if (UAnimSequence* Existing = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName)))
	{
		TPromise<UAnimSequence*> Promise;
		Promise.SetValue(Existing);
		return Promise.GetFuture();
	}

#if WITH_EDITOR
	// The clip itself is read here, synchronously, rather than in the worker
	// pump — TreSubsystem reads are thread-safe either way (confirmed:
	// FSWGTreArchive::Extract opens its own file handle per call), but the
	// request struct only needs to carry the already-decoded FSWGAnimationData
	// forward, keeping the queue element's shape uniform with the skeletal
	// mesh side (plain data in, no further TRE access needed downstream).
	FSWGAnimationData ClipAnimation;
	if (!FSWGAnimationReader::ReadAnimation(Owner.TreSubsystem->CreateIffReader(ClipPath), ClipAnimation))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to decode locomotion clip '%s' while building '%s'"), *ClipPath, *PackagePath);
		TPromise<UAnimSequence*> Promise;
		Promise.SetValue(nullptr);
		return Promise.GetFuture();
	}

	TSharedPtr<TPromise<UAnimSequence*>> Promise = MakeShared<TPromise<UAnimSequence*>>();
	TFuture<UAnimSequence*> Future = Promise->GetFuture();

	FSWGAnimSequenceBuildRequest Request;
	Request.SkeletonPath = SkeletonPath;
	Request.MeshVirtualPaths = MeshVirtualPaths;
	Request.ClipPath = ClipPath;
	Request.Skeleton = Skeleton;
	Request.ClipAnimation = MoveTemp(ClipAnimation);
	Request.TargetSkeleton = TargetSkeleton;
	Request.PackagePath = PackagePath;
	Request.Promise = MoveTemp(Promise);
	PendingAnimSequenceRequests.Enqueue(MoveTemp(Request));

	return Future;
#else
	UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: generated anim sequence '%s' isn't built yet and can't be built in a packaged build"), *PackagePath);
	TPromise<UAnimSequence*> Promise;
	Promise.SetValue(nullptr);
	return Promise.GetFuture();
#endif
}

#if WITH_EDITOR
void FSWGSkeletalAnimationPipeline::PumpAnimSequenceWork()
{
	for (;;)
	{
		if (bShuttingDown.load())
		{
			break;
		}
		if (PendingAnimSequenceFinalizeCount.load() >= MaxPendingFinalize)
		{
			break;
		}

		// See PumpSkeletalMeshWork's matching comment — Request must not be
		// constructed unless Dequeue is actually going to populate it.
		if (PendingAnimSequenceRequests.IsEmpty())
		{
			break;
		}
		FSWGAnimSequenceBuildRequest Request;
		if (!PendingAnimSequenceRequests.Dequeue(Request))
		{
			break;
		}

		FSWGAnimSequenceBuildResult Result;
		Result.PackagePath = Request.PackagePath;
		Result.ClipPath = Request.ClipPath;
		Result.TargetSkeleton = Request.TargetSkeleton;
		Result.Promise = MoveTemp(Request.Promise);

		// The actual SLERP/LERP dense-track math — see
		// FSWGAnimationImporter::BuildAnimSequenceData's own comment for why
		// this half (and only this half) is worker-safe.
		Result.bBuilt = FSWGAnimationImporter::BuildAnimSequenceData(Request.ClipAnimation, Request.Skeleton, Result.BuildData);

		++PendingAnimSequenceFinalizeCount;
		CompletedAnimSequenceBuilds.Enqueue(MoveTemp(Result));
	}

	bAnimSequenceWorkerActive = false;
	--NumActiveWorkerTasks;
}
#else
void FSWGSkeletalAnimationPipeline::PumpAnimSequenceWork()
{
	bAnimSequenceWorkerActive = false;
	--NumActiveWorkerTasks;
}
#endif

void FSWGSkeletalAnimationPipeline::DrainCompletedAnimSequenceBuilds(int32 MaxToProcess)
{
	int32 Processed = 0;
	// See DrainCompletedSkeletalMeshBuilds's comment — same reason Result is
	// only constructed once we know Dequeue will actually populate it.
	while (Processed < MaxToProcess && !CompletedAnimSequenceBuilds.IsEmpty())
	{
		FSWGAnimSequenceBuildResult Result;
		if (!CompletedAnimSequenceBuilds.Dequeue(Result))
		{
			break;
		}

		--PendingAnimSequenceFinalizeCount;
		++Processed;

		if (!Result.bBuilt)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to build anim sequence data '%s' from '%s'"), *Result.PackagePath, *Result.ClipPath);
			Result.Promise->SetValue(nullptr);
			continue;
		}

		UAnimSequence* Sequence = FSWGAnimationImporter::FinalizeAnimSequence(MoveTemp(Result.BuildData), Result.TargetSkeleton, Result.PackagePath);
		if (!Sequence)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to finalize generated anim sequence '%s' from '%s'"), *Result.PackagePath, *Result.ClipPath);
		}
		Result.Promise->SetValue(Sequence);
	}
}

UBlendSpace* FSWGSkeletalAnimationPipeline::GetOrBuildLocomotionBlendSpace(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FSWGLocomotionClipSet& ClipSet, UAnimSequence* IdleSequence, UAnimSequence* WalkSequence, UAnimSequence* RunSequence, float WalkSpeed, float RunSpeed, USkeleton* TargetSkeleton)
{
	// The clip paths rather than the loop animation name: two postures can
	// resolve to different names over the same three clips (a species whose
	// .ash gives sneaking and crouched the same loop), and that should share
	// one built asset.
	const uint32 PathsHash = GetTypeHash(SkeletonPath) ^ GetTypeHash(MeshVirtualPaths)
		^ GetTypeHash(ClipSet.IdlePath) ^ GetTypeHash(ClipSet.WalkPath) ^ GetTypeHash(ClipSet.RunPath) ^ GeneratedAssetVersion;
	const FString AssetName = FString::Printf(TEXT("BS_%u"), PathsHash);
	const FString PackagePath = TEXT("/Game/SWGEmu/Generated/") + AssetName;

	if (UBlendSpace* Existing = LoadObject<UBlendSpace>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName)))
	{
		return Existing;
	}

#if WITH_EDITOR
	if (!IdleSequence || !WalkSequence || !RunSequence)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: missing a locomotion clip — can't build blend space '%s'"), *PackagePath);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();
	// UBlendSpace1D rather than the generic 3-axis UBlendSpace: all our
	// samples vary only along X (speed), and this subclass exposes that
	// intent directly — a plain bScaleAnimation bool instead of the base
	// class's protected AxisToScaleAnimation enum.
	UBlendSpace1D* Result = NewObject<UBlendSpace1D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	Result->SetSkeleton(TargetSkeleton);

	// Scale each sample's playback rate by how far the live blend input is
	// from that sample's own speed — same role the old ReferenceSpeed rate
	// clamp played, now owned by the engine instead of Tick().
	Result->bScaleAnimation = true;

	// BlendParameters is EditAnywhere but C++-protected (editor tooling
	// normally sets it through the Details panel, which goes through
	// FProperty reflection rather than direct member access) —
	// ContainerPtrToValuePtr is the same mechanism, just called from code.
	if (FStructProperty* BlendParametersProp = FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters")))
	{
		FBlendParameter* BlendParameters = BlendParametersProp->ContainerPtrToValuePtr<FBlendParameter>(Result);
		BlendParameters[0].DisplayName = TEXT("Speed");
		BlendParameters[0].Min = 0.0f;
		BlendParameters[0].Max = RunSpeed;
		BlendParameters[0].GridNum = 4;
	}

	Result->AddSample(IdleSequence, FVector(0.0f, 0.0f, 0.0f));
	Result->AddSample(WalkSequence, FVector(WalkSpeed, 0.0f, 0.0f));
	Result->AddSample(RunSequence, FVector(RunSpeed, 0.0f, 0.0f));

	// ResampleData(), not just ValidateSampleData(): adding samples only fills
	// SampleData (the authored sample list). The *runtime* structure the
	// evaluator actually reads — BlendSpaceData, the line segments derived
	// from those samples — is built solely by ResampleData (which validates
	// internally, then derives DimensionIndices from the sample AABB and runs
	// the 1D segmentation). Without it BlendSpaceData stays empty, so
	// GetSamplesFromBlendInput returns nothing at runtime and the evaluated
	// pose falls through as identity local rotations — which renders as a
	// T-pose for this skeleton, whose arms only come down once the composed
	// bind rotation is applied. Nothing warns: the asset itself still has
	// valid samples, a valid skeleton, and reports IsPlaying.
	Result->ResampleData();

	Result->MarkPackageDirty();
	Result->PostEditChange();
	FAssetRegistryModule::AssetCreated(Result);

	const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	UPackage::SavePackage(Package, Result, *FileName, SaveArgs);

	UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: built and cached generated blend space '%s' for skeleton '%s'"), *PackagePath, *SkeletonPath);
	return Result;
#else
	UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: generated blend space '%s' isn't built yet and can't be built in a packaged build"), *PackagePath);
	return nullptr;
#endif
}

void FSWGSkeletalAnimationPipeline::TryApplyGeneratedAnimatedMesh(AActor& Actor, const TArray<FString>& MeshVirtualPaths, const TMap<FString, FString>& AnimationLatPaths, UMeshComponent* ProceduralMeshComponent, const TMap<FString, FLinearColor>* PaletteTintOverrides, const TMap<FString, float>* MorphWeights, const TMap<FString, int32>* TextureIndexOverrides)
{
	if (!Owner.TreSubsystem)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(&Actor);
	USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
	if (!CharacterMesh)
	{
		return;
	}

	FString SkeletonPath;
	for (const FString& MeshPath : MeshVirtualPaths)
	{
		SkeletonPath = FSWGMeshReader::ReadSkeletalMeshSkeletonPath(Owner.TreSubsystem->CreateIffReader(MeshPath));
		if (!SkeletonPath.IsEmpty()) break;
	}
	if (SkeletonPath.IsEmpty())
	{
		// No SKTM skeleton reference — this object has no animation data at
		// all (most world objects), not an error; leave the procedural
		// bind-pose mesh in place.
		return;
	}

	const FString* LatPathPtr = AnimationLatPaths.Find(SkeletonPath);
	const FSWGLocomotionSource* Source = LatPathPtr ? GetOrLoadLocomotionSource(*LatPathPtr) : nullptr;
	if (!Source)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: no usable LATX locomotion LAT for skeleton '%s'"), *SkeletonPath);
		return;
	}
	const FString LatPath = *LatPathPtr;

	// The creature's baselines normally land before its mesh finishes
	// building, so this is usually its real posture already; if not, Tick's
	// UpdatePostureDrivenAnimations swaps to the right one as soon as base3
	// arrives.
	ESWGPosture Posture = ESWGPosture::Upright;
	int64 StateBitmask = 0;
	ReadPostureAndStates(Actor, Posture, StateBitmask);

	FSWGLocomotionClipSet ClipSet;
	if (!SWGLocomotion::ResolveClipSet(Source->Hierarchy, Source->Lat, Posture, StateBitmask, ClipSet))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: LAT '%s' has no usable clips for posture %d"), *LatPath, (int32)Posture);
		return;
	}

	FSWGSkeletonData Skeleton;
	if (!FSWGSkeletonReader::ReadSkeleton(Owner.TreSubsystem->CreateIffReader(SkeletonPath), Skeleton))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to parse SKTM skeleton '%s'"), *SkeletonPath);
		return;
	}

	// Everything below only runs once the generated skeletal mesh is ready.
	// RequestGeneratedSkeletalMesh's future may already be set (cache hit)
	// or resolve later, on the game thread, from DrainCompletedSkeletalMeshBuilds
	// (cache miss, worker build) — either way .Next()'s continuation always
	// runs on whichever thread calls Promise.SetValue(), which this pipeline
	// only ever does from Tick, so no extra thread-marshaling is needed here.
	// Actor/ProceduralMeshComponent are captured weakly regardless (either
	// can be destroyed/GC'd while a worker build is in flight); the override
	// maps are copied by value since the caller's pointers point at locals
	// (see USWGMeshGeneratorSubsystem::ProcessNextRequest) that won't survive
	// past this function returning.
	TWeakObjectPtr<AActor> ActorWeak(&Actor);
	TWeakObjectPtr<UMeshComponent> ProceduralMeshComponentWeak(ProceduralMeshComponent);
	const bool bHasPaletteTintOverrides = PaletteTintOverrides != nullptr;
	const bool bHasMorphWeights = MorphWeights != nullptr;
	const bool bHasTextureIndexOverrides = TextureIndexOverrides != nullptr;
	TMap<FString, FLinearColor> PaletteTintOverridesCopy = PaletteTintOverrides ? *PaletteTintOverrides : TMap<FString, FLinearColor>();
	TMap<FString, float> MorphWeightsCopy = MorphWeights ? *MorphWeights : TMap<FString, float>();
	TMap<FString, int32> TextureIndexOverridesCopy = TextureIndexOverrides ? *TextureIndexOverrides : TMap<FString, int32>();

	RequestGeneratedSkeletalMesh(SkeletonPath, MeshVirtualPaths, Skeleton)
		.Next([this, ActorWeak, MeshVirtualPaths, SkeletonPath, Skeleton, LatPath, ClipSet, Posture, StateBitmask, ProceduralMeshComponentWeak,
		 PaletteTintOverridesCopy, MorphWeightsCopy, TextureIndexOverridesCopy,
		 bHasPaletteTintOverrides, bHasMorphWeights, bHasTextureIndexOverrides](USkeletalMesh* GeneratedMesh)
		{
			AActor* Actor = ActorWeak.Get();
			if (!Actor || !GeneratedMesh)
			{
				return;
			}
			ACharacter* Character = Cast<ACharacter>(Actor);
			USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
			if (!CharacterMesh)
			{
				return;
			}

			const TMap<FString, FLinearColor>* PaletteTintOverrides = bHasPaletteTintOverrides ? &PaletteTintOverridesCopy : nullptr;
			const TMap<FString, float>* MorphWeights = bHasMorphWeights ? &MorphWeightsCopy : nullptr;
			const TMap<FString, int32>* TextureIndexOverrides = bHasTextureIndexOverrides ? &TextureIndexOverridesCopy : nullptr;
			UMeshComponent* ProceduralMeshComponent = ProceduralMeshComponentWeak.Get();

			// TargetSkeleton is the actual asset skeleton GeneratedMesh was built
			// against (may include merged face bones), which is what a
			// UAnimSequence/UBlendSpace played on this component must be
			// compatible with; Skeleton (the plain joint list) is what the
			// animated tracks themselves get built from.
			USkeleton* TargetSkeleton = GeneratedMesh->GetSkeleton();

			float WalkSpeed = 0.0f;
			float RunSpeed = 0.0f;
			ReadBlendSpeeds(*Character, WalkSpeed, RunSpeed);

			auto OnBlendSpaceReady = [this, ActorWeak, GeneratedMesh, TargetSkeleton, MeshVirtualPaths, SkeletonPath, Skeleton, LatPath, ClipSet, Posture, StateBitmask,
				ProceduralMeshComponentWeak, PaletteTintOverridesCopy, MorphWeightsCopy, TextureIndexOverridesCopy,
				bHasPaletteTintOverrides, bHasMorphWeights, bHasTextureIndexOverrides](UBlendSpace* LocomotionBlendSpace)
			{
				AActor* Actor = ActorWeak.Get();
				if (!Actor)
				{
					return;
				}
				ACharacter* Character = Cast<ACharacter>(Actor);
				USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
				if (!CharacterMesh)
				{
					return;
				}

				const TMap<FString, FLinearColor>* PaletteTintOverrides = bHasPaletteTintOverrides ? &PaletteTintOverridesCopy : nullptr;
				const TMap<FString, float>* MorphWeights = bHasMorphWeights ? &MorphWeightsCopy : nullptr;
				const TMap<FString, int32>* TextureIndexOverrides = bHasTextureIndexOverrides ? &TextureIndexOverridesCopy : nullptr;
				UMeshComponent* ProceduralMeshComponent = ProceduralMeshComponentWeak.Get();

				if (!LocomotionBlendSpace)
				{
					UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: %s has no usable locomotion blend space; leaving the procedural mesh visible instead of showing a T-pose"), *Actor->GetName());
					return;
				}

				CharacterMesh->SetSkeletalMesh(GeneratedMesh);
				CharacterMesh->SetVisibility(true);
				CharacterMesh->SetHiddenInGame(false);

				// SWG geometry (both the procedural generated-mesh path and this skeletal
				// mesh, which shares the same .mgn source data/authoring convention) is
				// authored feet-at-origin, but ACharacter's capsule is centered on the
				// actor — same fallback BuildGeneratedMeshComponent applies to the
				// procedural mesh when it has no bounds to compute a precise
				// center-based offset.
				if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
				{
					CharacterMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -Capsule->GetScaledCapsuleHalfHeight()));
				}

				// SWG's forward axis is 90 degrees off from Unreal's — the same quirk
				// ASWGPlayer's camera/control rotation already corrects for. That fix
				// doesn't touch the mesh itself, so rotate the whole mesh rigidly here.
				CharacterMesh->SetRelativeRotation(FRotator(0.0f, SWGCharacterMeshYaw, 0.0f));

				// The importer creates material slots named after each submesh's shader
				// path but leaves the actual material null — build/assign the same real
				// per-shader textured materials the procedural generated-mesh path
				// already uses.
				int32 NumSkeletalTextured = 0;
				for (int32 SlotIndex = 0; SlotIndex < GeneratedMesh->GetMaterials().Num(); ++SlotIndex)
				{
					const FString ShaderPath = ExtractShaderPathFromMaterialSlotName(GeneratedMesh->GetMaterials()[SlotIndex].MaterialSlotName.ToString());
					if (UMaterialInterface* Material = Owner.GetOrBuildObjectMaterial(ShaderPath, PaletteTintOverrides, TextureIndexOverrides))
					{
						CharacterMesh->SetMaterial(SlotIndex, Material);
						++NumSkeletalTextured;
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: %s skeletal mesh slot %d ('%s') has no real material — leaving the mesh's default"),
							*Actor->GetName(), SlotIndex, *ShaderPath);
					}
				}
				UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: %s skeletal mesh assigned %d/%d real textured material(s)"),
					*Actor->GetName(), NumSkeletalTextured, GeneratedMesh->GetMaterials().Num());

				// Real per-character face/body shape — a no-op for any name
				// GeneratedMesh doesn't actually have a matching morph target
				// for (SetMorphTarget just logs nothing and does nothing).
				if (MorphWeights)
				{
					for (const TPair<FString, float>& Pair : *MorphWeights)
					{
						CharacterMesh->SetMorphTarget(FName(*Pair.Key), Pair.Value);
					}
					UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: applied %d morph weight(s) to '%s' on mesh '%s'"),
						MorphWeights->Num(), *Actor->GetName(), *GetNameSafe(CharacterMesh->GetSkeletalMeshAsset()));
				}

				CharacterMesh->PlayAnimation(LocomotionBlendSpace, true);
				UAnimSingleNodeInstance* AnimInstance = Cast<UAnimSingleNodeInstance>(CharacterMesh->GetAnimInstance());
				if (!AnimInstance)
				{
					UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: %s failed to start blend space playback; leaving the procedural mesh visible instead of showing a T-pose"), *Actor->GetName());
					CharacterMesh->SetVisibility(false);
					CharacterMesh->SetHiddenInGame(true);
					if (ProceduralMeshComponent)
					{
						ProceduralMeshComponent->SetVisibility(true);
						ProceduralMeshComponent->SetHiddenInGame(false);
					}
					return;
				}

				FSWGPlayingAnimation Playing;
				Playing.MeshComponent = CharacterMesh;
				Playing.AnimInstance = AnimInstance;
				Playing.TargetSkeleton = TargetSkeleton;
				Playing.SkeletonPath = SkeletonPath;
				Playing.MeshVirtualPaths = MeshVirtualPaths;
				Playing.LatPath = LatPath;
				Playing.Skeleton = Skeleton;
				Playing.ClipSet = ClipSet;
				Playing.Posture = Posture;
				Playing.StateBitmask = StateBitmask;
				PlayingAnimations.Add(MoveTemp(Playing));

				// A player can receive more than one asynchronous mesh request (and a
				// previous PIE run can leave an actor-owned procedural component around).
				// Hiding only the component from *this* request left an older bind-pose
				// mesh rendering through the generated skeletal mesh, which looked like a
				// second, incorrectly UV-mapped face at the Wookiee's waist. The
				// procedural fallback is a UStaticMeshComponent now (see
				// BuildGeneratedMeshComponent), not a UDynamicMeshComponent.
				TInlineComponentArray<UStaticMeshComponent*> ProceduralMeshComponents(Actor);
				for (UStaticMeshComponent* ProceduralMesh : ProceduralMeshComponents)
				{
					if (ProceduralMesh)
					{
						ProceduralMesh->SetVisibility(false);
						ProceduralMesh->SetHiddenInGame(true);
					}
				}

				UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: %s resolved to the generated skeletal mesh for skeleton '%s'"), *Actor->GetName(), *SkeletonPath);
			};

			RequestLocomotionBlendSpace(SkeletonPath, MeshVirtualPaths, Skeleton, TargetSkeleton, ClipSet, WalkSpeed, RunSpeed, OnBlendSpaceReady);
		});
}
