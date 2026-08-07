#include "Subsystems/SWGSkeletalAnimationPipeline.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGIFFChunkReader.h"
#include "TRE/SWGAnimationReader.h"
#include "TRE/SWGSlotDefinitionReader.h"
#include "Import/SWGSkeletalMeshImporter.h"
#include "Import/SWGAnimationImporter.h"
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
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MeshUtilities.h"
#include "Async/Async.h"

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

	bool ReadDefaultLocomotionPaths(const FSWGIffReader& Reader, TArray<FString>& OutPaths)
	{
		TArray<FString> Clips;
		auto Visit = [&Reader, &Clips](auto&& Self, const FSWGIffChunk& Node) -> void
		{
			if (Node.IsForm())
			{
				for (const FSWGIffChunk& Child : Reader.ReadChildren(Node)) Self(Self, Child);
				return;
			}

			FSWGIFFChunkReader ChunkReader(Node, Reader);
			const FString Value = ChunkReader.ReadTerminiatedString();

			if (Value.EndsWith(TEXT(".ans"), ESearchCase::IgnoreCase))
			{
				Clips.AddUnique(Value);
			}
		};

		for (const FSWGIffChunk& Root : Reader.ReadChunks()) Visit(Visit, Root);
		auto FindClip = [&Clips](const TCHAR* Needle) -> FString
		{
			const FString* Found = Clips.FindByPredicate([Needle](const FString& Path) { return Path.Contains(Needle, ESearchCase::IgnoreCase); });
			return Found ? *Found : FString();
		};

		const FString Idle = FindClip(TEXT("_idl_breathe_normally."));
		const FString Walk = FindClip(TEXT("_loc_walk"));
		const FString Run = FindClip(TEXT("_loc_run."));
		if (Idle.IsEmpty() || Walk.IsEmpty() || Run.IsEmpty()) return false;

		OutPaths = { Idle, Walk, Walk, Run }; // LAT provides a parametric walk/run pair, not a distinct jog clip.
		return true;
	}
}

FSWGSkeletalAnimationPipeline::FSWGSkeletalAnimationPipeline(USWGMeshGeneratorSubsystem& InOwner)
	: Owner(InOwner)
{
}

void FSWGSkeletalAnimationPipeline::Tick(float DeltaTime)
{
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

		const ACharacter* Character = Cast<ACharacter>(MeshComponent->GetOwner());
		float HorizontalSpeed = Character ? Character->GetVelocity().Size2D() : 0.0f;
		if (const USWGMovementComponent* Movement = Character ? Cast<USWGMovementComponent>(Character->GetCharacterMovement()) : nullptr)
		{
			// LastNetworkUpdateTime > 0 means this is a network-driven actor
			// (see its own comment) — the server just stops sending updates
			// when it stops moving rather than sending an explicit zero-speed
			// one, so a stale update means "stopped," not "still going at the
			// last derived speed." Actors that have never taken a network
			// update (the locally-controlled player) skip this entirely and
			// keep using their real CharacterMovementComponent velocity.
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

void FSWGSkeletalAnimationPipeline::GetOrBuildGeneratedSkeletalMeshAsync(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FSWGSkeletonData& Skeleton, TFunction<void(USkeletalMesh*)> OnComplete)
{
	const uint32 PathsHash = GetTypeHash(SkeletonPath) ^ GetTypeHash(MeshVirtualPaths);
	const FString AssetName = FString::Printf(TEXT("SK_%u"), PathsHash);
	const FString PackagePath = TEXT("/Game/SWGEmu/Generated/") + AssetName;

	// The saved .uasset on disk is the cache — a hit here skips rebuilding
	// entirely, same role GetOrBuildGeneratedStaticMesh's own cache plays.
	// LoadObject is a game-thread-only asset op, so this check has to happen
	// here, synchronously, before any work is handed to a worker thread.
	if (USkeletalMesh* Existing = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName)))
	{
		OnComplete(Existing);
		return;
	}

#if WITH_EDITOR
	if (InFlightSkeletalMeshBuilds >= MaxInFlightSkeletalMeshBuilds)
	{
		UE_LOG(LogTemp, Verbose, TEXT("FSWGSkeletalAnimationPipeline: too many in-flight skeletal mesh builds (%d/%d), deferring '%s' — caller will retry on the next mesh-relevant event"),
			InFlightSkeletalMeshBuilds, MaxInFlightSkeletalMeshBuilds, *PackagePath);
		OnComplete(nullptr);
		return;
	}

	// MeshUtilities is loaded once on the game thread in
	// USWGMeshGeneratorSubsystem::Initialize() and never unloaded during a
	// session — safe to hand the raw pointer to a worker thread from here on.
	IMeshUtilities* MeshUtilitiesModule = Owner.MeshUtilities;
	check(MeshUtilitiesModule);

	// GetSlotHardpoints() lazily parses slot_definitions.iff into
	// SlotHardpoints on first use (mutates this object's state) — force that
	// to happen here, on the game thread, and hand the worker thread a
	// plain copy instead of letting it touch this object's state directly.
	const TMap<FString, FString> SlotHardpointsCopy = GetSlotHardpoints();

	++InFlightSkeletalMeshBuilds;
	Async(EAsyncExecution::ThreadPool, [this, SkeletonPath, MeshVirtualPaths, Skeleton, PackagePath, SlotHardpointsCopy, MeshUtilitiesModule, OnComplete]()
	{
		FSWGSkeletonData MeshBuildSkeleton = Skeleton;
		const FString FaceSkeletonPath = DeriveFaceSkeletonPath(MeshVirtualPaths);
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
		MeshParts.Reserve(MeshVirtualPaths.Num());
		for (const FString& MeshPath : MeshVirtualPaths)
		{
			FSWGMeshData PartData;
			if (FSWGMeshReader::ReadSkeletalMeshBindPose(Owner.TreSubsystem->CreateIffReader(MeshPath), PartData))
			{
				MeshParts.Add(MoveTemp(PartData));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to parse skeletal mesh part '%s' while building generated mesh for skeleton '%s'"), *MeshPath, *SkeletonPath);
			}
		}
		if (MeshParts.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: no usable mesh parts to build a generated skeletal mesh for skeleton '%s'"), *SkeletonPath);
			AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
			{
				--InFlightSkeletalMeshBuilds;
				OnComplete(nullptr);
			});
			return;
		}

		TArray<const FSWGMeshData*> MeshPartPtrs;
		MeshPartPtrs.Reserve(MeshParts.Num());
		for (const FSWGMeshData& Part : MeshParts)
		{
			MeshPartPtrs.Add(&Part);
		}

		// The actual expensive geometry-build pass — FMeshUtilities::
		// BuildSkeletalMesh underneath, confirmed thread-safe (see
		// mesh-threading-plan.html's engine-research table). MeshPartPtrs
		// only needs to outlive this call, so it's fine that it (and
		// MeshParts) don't survive past this worker task.
		FSWGSkeletalMeshBuildData BuildData;
		const bool bBuilt = FSWGSkeletalMeshImporter::BuildSkeletalMeshData(*MeshUtilitiesModule, MeshBuildSkeleton, MeshPartPtrs, SlotHardpointsCopy, PackagePath, BuildData);

		// Everything from here on is UObject/package/asset-registry work —
		// must run on the game thread (see FSWGSkeletalMeshImporter::
		// FinalizeSkeletalMesh's own comment).
		AsyncTask(ENamedThreads::GameThread, [this, bBuilt, BuildData = MoveTemp(BuildData), PackagePath, SkeletonPath, MeshVirtualPaths, OnComplete]() mutable
		{
			--InFlightSkeletalMeshBuilds;

			if (!bBuilt)
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to build generated skeletal mesh data '%s' for skeleton '%s'"), *PackagePath, *SkeletonPath);
				OnComplete(nullptr);
				return;
			}

			USkeletalMesh* Result = FSWGSkeletalMeshImporter::FinalizeSkeletalMesh(MoveTemp(BuildData), PackagePath);
			if (!Result)
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to finalize generated skeletal mesh '%s' for skeleton '%s'"), *PackagePath, *SkeletonPath);
				OnComplete(nullptr);
				return;
			}

			UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: built and cached generated skeletal mesh '%s' for skeleton '%s'"), *PackagePath, *SkeletonPath);

			// FinalizeSkeletalMesh already saved its package before
			// returning, so the user data has to be added and the package
			// re-saved here — otherwise it'd only live in memory for this
			// session and be missing again on the next cold LoadObject
			// cache-hit.
			USWGMeshSourceUserData* SourceData = NewObject<USWGMeshSourceUserData>(Result);
			SourceData->DebugName = FString::Printf(TEXT("%s (skeleton %s)"), *FString::Join(MeshVirtualPaths, TEXT(", ")), *SkeletonPath);
			Result->AddAssetUserData(SourceData);

			if (UPackage* ResultPackage = Result->GetPackage())
			{
				ResultPackage->MarkPackageDirty();
				const FString FileName = FPackageName::LongPackageNameToFilename(ResultPackage->GetName(), FPackageName::GetAssetPackageExtension());
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
				SaveArgs.SaveFlags = SAVE_NoError;
				UPackage::SavePackage(ResultPackage, Result, *FileName, SaveArgs);
			}

			OnComplete(Result);
		});
	});
#else
	UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: generated skeletal mesh '%s' isn't built yet and can't be built in a packaged build — leaving skeleton '%s' on its procedural bind-pose mesh"), *PackagePath, *SkeletonPath);
	OnComplete(nullptr);
#endif
}

void FSWGSkeletalAnimationPipeline::GetOrBuildLocomotionAnimSequenceAsync(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FString& ClipPath, const FSWGSkeletonData& Skeleton, USkeleton* TargetSkeleton, TFunction<void(UAnimSequence*)> OnComplete)
{
	const uint32 PathsHash = GetTypeHash(SkeletonPath) ^ GetTypeHash(MeshVirtualPaths) ^ GetTypeHash(ClipPath);
	const FString AssetName = FString::Printf(TEXT("AS_%u"), PathsHash);
	const FString PackagePath = TEXT("/Game/SWGEmu/Generated/") + AssetName;

	// LoadObject is a game-thread-only asset op, so the cache-hit check has
	// to happen here, synchronously, before any work is handed to a worker.
	if (UAnimSequence* Existing = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName)))
	{
		OnComplete(Existing);
		return;
	}

#if WITH_EDITOR
	if (InFlightAnimSequenceBuilds >= MaxInFlightAnimSequenceBuilds)
	{
		UE_LOG(LogTemp, Verbose, TEXT("FSWGSkeletalAnimationPipeline: too many in-flight anim sequence builds (%d/%d), deferring '%s' — caller will retry on the next mesh-relevant event"),
			InFlightAnimSequenceBuilds, MaxInFlightAnimSequenceBuilds, *PackagePath);
		OnComplete(nullptr);
		return;
	}

	FSWGAnimationData ClipAnimation;
	if (!FSWGAnimationReader::ReadAnimation(Owner.TreSubsystem->CreateIffReader(ClipPath), ClipAnimation))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to decode locomotion clip '%s' while building '%s'"), *ClipPath, *PackagePath);
		OnComplete(nullptr);
		return;
	}

	// TargetSkeleton (a UObject) is only touched once we're back on the game
	// thread (FinalizeAnimSequence) — the worker task below only carries
	// plain data (ClipAnimation/Skeleton, already read/copied) forward.
	++InFlightAnimSequenceBuilds;
	Async(EAsyncExecution::ThreadPool, [this, ClipAnimation, Skeleton, PackagePath, ClipPath, TargetSkeleton, OnComplete]()
	{
		// The actual SLERP/LERP dense-track math — see
		// FSWGAnimationImporter::BuildAnimSequenceData's own comment for why
		// this half (and only this half) is worker-safe.
		FSWGAnimSequenceBuildData BuildData;
		const bool bBuilt = FSWGAnimationImporter::BuildAnimSequenceData(ClipAnimation, Skeleton, BuildData);

		AsyncTask(ENamedThreads::GameThread, [this, bBuilt, BuildData = MoveTemp(BuildData), PackagePath, ClipPath, TargetSkeleton, OnComplete]() mutable
		{
			--InFlightAnimSequenceBuilds;

			if (!bBuilt)
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to build anim sequence data '%s' from '%s'"), *PackagePath, *ClipPath);
				OnComplete(nullptr);
				return;
			}

			UAnimSequence* Result = FSWGAnimationImporter::FinalizeAnimSequence(MoveTemp(BuildData), TargetSkeleton, PackagePath);
			if (!Result)
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to finalize generated anim sequence '%s' from '%s'"), *PackagePath, *ClipPath);
			}
			OnComplete(Result);
		});
	});
#else
	UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: generated anim sequence '%s' isn't built yet and can't be built in a packaged build"), *PackagePath);
	OnComplete(nullptr);
#endif
}

UBlendSpace* FSWGSkeletalAnimationPipeline::GetOrBuildLocomotionBlendSpace(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const TArray<FString>& LocomotionPaths, UAnimSequence* IdleSequence, UAnimSequence* WalkSequence, UAnimSequence* RunSequence, float WalkSpeed, float RunSpeed, USkeleton* TargetSkeleton)
{
	const uint32 PathsHash = GetTypeHash(SkeletonPath) ^ GetTypeHash(MeshVirtualPaths) ^ GetTypeHash(LocomotionPaths);
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

	const FString* LatPath = AnimationLatPaths.Find(SkeletonPath);
	TArray<FString> LocomotionPaths;
	if (!LatPath || !ReadDefaultLocomotionPaths(Owner.TreSubsystem->CreateIffReader(*LatPath), LocomotionPaths))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: no usable LATX locomotion LAT for skeleton '%s'"), *SkeletonPath);
		return;
	}

	FSWGSkeletonData Skeleton;
	if (!FSWGSkeletonReader::ReadSkeleton(Owner.TreSubsystem->CreateIffReader(SkeletonPath), Skeleton))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalAnimationPipeline: failed to parse SKTM skeleton '%s'"), *SkeletonPath);
		return;
	}

	// Everything below only runs once the generated skeletal mesh is ready.
	// GetOrBuildGeneratedSkeletalMeshAsync may finish synchronously (cache
	// hit) or asynchronously, later, on the game thread (cache miss, worker
	// build) — either way the continuation below must not depend on this
	// stack frame still being alive. Actor/ProceduralMeshComponent are
	// captured weakly (either can be destroyed/GC'd while a worker build is
	// in flight); the override maps are copied by value since the caller's
	// pointers point at locals (see USWGMeshGeneratorSubsystem::
	// ProcessNextRequest) that won't survive past this function returning.
	TWeakObjectPtr<AActor> ActorWeak(&Actor);
	TWeakObjectPtr<UMeshComponent> ProceduralMeshComponentWeak(ProceduralMeshComponent);
	const bool bHasPaletteTintOverrides = PaletteTintOverrides != nullptr;
	const bool bHasMorphWeights = MorphWeights != nullptr;
	const bool bHasTextureIndexOverrides = TextureIndexOverrides != nullptr;
	TMap<FString, FLinearColor> PaletteTintOverridesCopy = PaletteTintOverrides ? *PaletteTintOverrides : TMap<FString, FLinearColor>();
	TMap<FString, float> MorphWeightsCopy = MorphWeights ? *MorphWeights : TMap<FString, float>();
	TMap<FString, int32> TextureIndexOverridesCopy = TextureIndexOverrides ? *TextureIndexOverrides : TMap<FString, int32>();

	GetOrBuildGeneratedSkeletalMeshAsync(SkeletonPath, MeshVirtualPaths, Skeleton,
		[this, ActorWeak, MeshVirtualPaths, SkeletonPath, Skeleton, LocomotionPaths, ProceduralMeshComponentWeak,
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
			// against (may include merged face bones — see
			// GetOrBuildGeneratedSkeletalMeshAsync), which is what a
			// UAnimSequence/UBlendSpace played on this component must be
			// compatible with; Skeleton (the plain joint list) is what the
			// animated tracks themselves get built from.
			USkeleton* TargetSkeleton = GeneratedMesh->GetSkeleton();

			const USWGMovementComponent* Movement = Cast<USWGMovementComponent>(Character->GetCharacterMovement());
			const float WalkSpeed = Movement && Movement->WalkSpeed > KINDA_SMALL_NUMBER
				? Movement->WalkSpeed * 100.0f
				: 155.0f;
			const float RunSpeed = Movement && Movement->RunSpeed > Movement->WalkSpeed
				? Movement->RunSpeed * 100.0f
				: FMath::Max(WalkSpeed * 2.0f, Movement ? Movement->MaxWalkSpeed : 310.0f);

			// Idle/Walk/Run each build independently and asynchronously (see
			// GetOrBuildLocomotionAnimSequenceAsync) — this join runs the
			// rest of the function (blend space + attach) exactly once,
			// after all three land, regardless of which order they actually
			// complete in. All three OnComplete callbacks fire on the game
			// thread, so NumCompleted needs no synchronization.
			struct FSWGLocomotionJoinState
			{
				UAnimSequence* IdleSequence = nullptr;
				UAnimSequence* WalkSequence = nullptr;
				UAnimSequence* RunSequence = nullptr;
				int32 NumCompleted = 0;
			};
			TSharedRef<FSWGLocomotionJoinState> JoinState = MakeShared<FSWGLocomotionJoinState>();

			auto OnOneAnimSequenceReady = [this, JoinState, ActorWeak, GeneratedMesh, TargetSkeleton, MeshVirtualPaths, SkeletonPath, LocomotionPaths,
				ProceduralMeshComponentWeak, PaletteTintOverridesCopy, MorphWeightsCopy, TextureIndexOverridesCopy,
				bHasPaletteTintOverrides, bHasMorphWeights, bHasTextureIndexOverrides, WalkSpeed, RunSpeed]()
			{
				if (JoinState->NumCompleted < 3)
				{
					return;
				}

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

				UBlendSpace* LocomotionBlendSpace = GetOrBuildLocomotionBlendSpace(SkeletonPath, MeshVirtualPaths, LocomotionPaths, JoinState->IdleSequence, JoinState->WalkSequence, JoinState->RunSequence, WalkSpeed, RunSpeed, TargetSkeleton);
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
				CharacterMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

				// The importer creates material slots named after each submesh's shader
				// path (see FSWGSkeletalMeshImporter::FinalizeSkeletalMesh) but leaves
				// the actual material null — build/assign the same real per-shader
				// textured materials the procedural generated-mesh path already uses.
				int32 NumSkeletalTextured = 0;
				for (int32 SlotIndex = 0; SlotIndex < GeneratedMesh->GetMaterials().Num(); ++SlotIndex)
				{
					const FString ShaderPath = GeneratedMesh->GetMaterials()[SlotIndex].MaterialSlotName.ToString();
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

				// Real per-character face/body shape (see ResolveCustomizationMorphWeights)
				// — a no-op for any name GeneratedMesh doesn't actually have a matching
				// morph target for (SetMorphTarget just logs nothing and does nothing).
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

			GetOrBuildLocomotionAnimSequenceAsync(SkeletonPath, MeshVirtualPaths, LocomotionPaths[0], Skeleton, TargetSkeleton,
				[JoinState, OnOneAnimSequenceReady](UAnimSequence* Seq) { JoinState->IdleSequence = Seq; ++JoinState->NumCompleted; OnOneAnimSequenceReady(); });
			GetOrBuildLocomotionAnimSequenceAsync(SkeletonPath, MeshVirtualPaths, LocomotionPaths[1], Skeleton, TargetSkeleton,
				[JoinState, OnOneAnimSequenceReady](UAnimSequence* Seq) { JoinState->WalkSequence = Seq; ++JoinState->NumCompleted; OnOneAnimSequenceReady(); });
			GetOrBuildLocomotionAnimSequenceAsync(SkeletonPath, MeshVirtualPaths, LocomotionPaths[3], Skeleton, TargetSkeleton,
				[JoinState, OnOneAnimSequenceReady](UAnimSequence* Seq) { JoinState->RunSequence = Seq; ++JoinState->NumCompleted; OnOneAnimSequenceReady(); });
		});
}
