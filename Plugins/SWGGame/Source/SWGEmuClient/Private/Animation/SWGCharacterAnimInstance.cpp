#include "Animation/SWGCharacterAnimInstance.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimationRuntime.h"
#include "Animation/AnimationPoseData.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Animation/SWGAnimationLayerLogic.h"

bool USWGCharacterAnimInstance::bLookAtEnabled = true;
bool USWGCharacterAnimInstance::bAimEnabled = true;

const FName USWGCharacterAnimInstance::HeadBoneName(TEXT("head"));
const FName USWGCharacterAnimInstance::AimBoneName(TEXT("spine3"));
const FName USWGCharacterAnimInstance::RiderSpineBoneName(TEXT("spine1"));

namespace
{
	/** Below this a layer is treated as absent (it still updates, so its clip keeps advancing). */
	constexpr float MinLayerWeight = 0.001f;

	/** Combat-pose aim and head turns share the retail head-turn speed so neither snaps. */
	const SWGAnimLayer::FLookAtLimits ModifierLimits{};
}

// ---------------------------------------------------------------------------
// Instance (game thread)
// ---------------------------------------------------------------------------

void USWGCharacterAnimInstance::SetLocomotion(UBlendSpace* BlendSpace, float PlayRate)
{
	LocomotionSpace = BlendSpace;
	LocomotionPlayRate = SWGAnimLayer::EffectiveTimeScale(PlayRate);
	++LocomotionSerial;
}

void USWGCharacterAnimInstance::SetLocomotionInput(const FVector2D& Position)
{
	LocomotionPosition = Position;
}

void USWGCharacterAnimInstance::PlayBaseOneShot(UAnimSequence* Sequence, float PlayRate, float BlendIn, float BlendOut)
{
	if (!Sequence)
	{
		return;
	}
	OneShotSequence = Sequence;
	OneShotPlayRate = SWGAnimLayer::EffectiveTimeScale(PlayRate);
	OneShotBlendIn = BlendIn;
	OneShotBlendOut = BlendOut;
	bOneShotHold = false;
	++OneShotSerial;
}

void USWGCharacterAnimInstance::HoldBasePose(UAnimSequence* Sequence, float NormalizedPosition)
{
	if (!Sequence)
	{
		return;
	}
	OneShotSequence = Sequence;
	OneShotPlayRate = 1.0f;
	OneShotBlendIn = 0.05f;
	OneShotBlendOut = 0.15f;
	OneShotHoldPosition = FMath::Clamp(NormalizedPosition, 0.0f, 1.0f);
	bOneShotHold = true;
	++OneShotSerial;
}

void USWGCharacterAnimInstance::ReleaseBasePose(float BlendOut)
{
	OneShotBlendOut = BlendOut;
	++OneShotReleaseSerial;
}

void USWGCharacterAnimInstance::PlayOverlay(UAnimSequence* Sequence, bool bAdditive, const TArray<FName>& MaskBones, float PlayRate, float BlendIn, float BlendOut)
{
	if (!Sequence)
	{
		return;
	}
	if (bAdditive)
	{
		AdditiveSequence = Sequence;
		AdditiveBones = MaskBones;
		AdditivePlayRate = SWGAnimLayer::EffectiveTimeScale(PlayRate);
		AdditiveBlendIn = BlendIn;
		AdditiveBlendOut = BlendOut;
		++AdditiveSerial;
	}
	else
	{
		MaskedSequence = Sequence;
		MaskedBones = MaskBones;
		MaskedPlayRate = SWGAnimLayer::EffectiveTimeScale(PlayRate);
		MaskedBlendIn = BlendIn;
		MaskedBlendOut = BlendOut;
		++MaskedSerial;
	}
}

void USWGCharacterAnimInstance::StopOverlay(bool bAdditive)
{
	if (bAdditive)
	{
		++AdditiveStopSerial;
	}
	else
	{
		++MaskedStopSerial;
	}
}

void USWGCharacterAnimInstance::SetLookAtTarget(bool bActive, const FVector& WorldTarget)
{
	bLookAtActive = bActive;
	LookAtTarget = WorldTarget;
}

void USWGCharacterAnimInstance::SetAimTarget(bool bActive, const FVector& WorldTarget)
{
	bAimActive = bActive;
	AimTarget = WorldTarget;
}

void USWGCharacterAnimInstance::SetRiderSpineLean(const FRotator& ComponentSpaceLean)
{
	RiderSpineLean = ComponentSpaceLean;
}

// ---------------------------------------------------------------------------
// Graph (any thread)
// ---------------------------------------------------------------------------

namespace
{
	/** One timed clip layer: the action track's one-shot, or one of the two overlays. */
	struct FSWGTimedLayer
	{
		FAnimNode_SequencePlayer_Standalone Player;
		TObjectPtr<UAnimSequence> Sequence;
		bool bAdditive = false;
		bool bActive = false;
		bool bPendingInit = false;
		bool bHold = false;
		float HoldPosition = 0.0f;
		float ReleaseTimer = -1.0f;
		float Elapsed = 0.0f;
		float Length = 0.0f;
		float Rate = 1.0f;
		float BlendIn = 0.0f;
		float BlendOut = 0.0f;
		float Weight = 0.0f;

		TArray<FName> MaskBones;
		TArray<float> BoneWeights;
		uint16 WeightsSerial = MAX_uint16;
		bool bHasMask = false;

		void Start(UAnimSequence* NewSequence, bool bInAdditive, const TArray<FName>& InMaskBones, float InRate, float InBlendIn, float InBlendOut, bool bInHold, float InHoldPosition)
		{
			Sequence = NewSequence;
			bAdditive = bInAdditive;
			MaskBones = InMaskBones;
			bHasMask = MaskBones.Num() > 0;
			WeightsSerial = MAX_uint16;
			Rate = InRate;
			BlendIn = InBlendIn;
			BlendOut = InBlendOut;
			bHold = bInHold;
			HoldPosition = InHoldPosition;
			ReleaseTimer = -1.0f;
			Length = NewSequence ? NewSequence->GetPlayLength() : 0.0f;
			Elapsed = 0.0f;
			Weight = 0.0f;
			bActive = NewSequence != nullptr && Length > KINDA_SMALL_NUMBER;
			bPendingInit = bActive;

			Player.SetSequence(NewSequence);
			Player.SetLoopAnimation(false);
			Player.SetStartPosition(0.0f);
			Player.SetPlayRate(bHold ? 0.0f : Rate);
		}

		void Stop(float OutDuration)
		{
			if (!bActive)
			{
				return;
			}
			// A held pose fades out by its release timer; a playing clip is cut to its last BlendOut seconds.
			BlendOut = OutDuration;
			if (bHold)
			{
				ReleaseTimer = 0.0f;
			}
			else
			{
				const float RealLength = Length / Rate;
				Elapsed = FMath::Max(Elapsed, RealLength - FMath::Max(OutDuration, KINDA_SMALL_NUMBER));
			}
		}

		void Update(const FAnimationUpdateContext& Context, float DeltaSeconds)
		{
			if (!bActive)
			{
				Weight = 0.0f;
				return;
			}

			if (bPendingInit)
			{
				FAnimationInitializeContext InitContext(Context.AnimInstanceProxy);
				Player.Initialize_AnyThread(InitContext);
				if (bHold)
				{
					Player.SetAccumulatedTime(HoldPosition * Length);
				}
				bPendingInit = false;
			}

			Elapsed += DeltaSeconds;
			if (bHold)
			{
				if (ReleaseTimer >= 0.0f)
				{
					ReleaseTimer += DeltaSeconds;
					Weight = BlendOut > KINDA_SMALL_NUMBER ? 1.0f - ReleaseTimer / BlendOut : 0.0f;
					if (Weight <= 0.0f)
					{
						bActive = false;
						Weight = 0.0f;
						return;
					}
				}
				else
				{
					Weight = BlendIn > KINDA_SMALL_NUMBER ? FMath::Min(1.0f, Elapsed / BlendIn) : 1.0f;
				}
			}
			else
			{
				const float RealLength = Length / Rate;
				if (Elapsed >= RealLength)
				{
					bActive = false;
					Weight = 0.0f;
					return;
				}
				Weight = SWGAnimLayer::OverlayWeight(Elapsed, RealLength, BlendIn, BlendOut);
			}

			Player.Update_AnyThread(Context.FractionalWeight(FMath::Max(Weight, MinLayerWeight)));
		}

		bool IsVisible() const { return bActive && Weight > MinLayerWeight; }

		/** Weight per compact pose bone from the mask joints; rebuilt when the bone container changes (LOD). */
		void EnsureBoneWeights(const FBoneContainer& BoneContainer)
		{
			if (!bHasMask || WeightsSerial == BoneContainer.GetSerialNumber())
			{
				return;
			}
			WeightsSerial = BoneContainer.GetSerialNumber();
			BoneWeights.Init(0.0f, BoneContainer.GetCompactPoseNumBones());
			for (const FName& BoneName : MaskBones)
			{
				const int32 MeshBoneIndex = BoneContainer.GetPoseBoneIndexForBoneName(BoneName);
				if (MeshBoneIndex == INDEX_NONE)
				{
					continue;
				}
				const FCompactPoseBoneIndex CompactIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));
				if (CompactIndex != INDEX_NONE)
				{
					BoneWeights[CompactIndex.GetInt()] = 1.0f;
				}
			}
		}
	};

	/** Rotates Bone by a component-space quaternion about its own origin, leaving its children's local transforms alone so they follow. */
	void RotateBoneInComponentSpace(FCompactPose& Pose, FCSPose<FCompactPose>& ComponentPose, FCompactPoseBoneIndex Bone, const FQuat& ComponentRotation)
	{
		const FBoneContainer& BoneContainer = Pose.GetBoneContainer();
		FQuat ParentRotation = FQuat::Identity;
		const FCompactPoseBoneIndex Parent = BoneContainer.GetParentBoneIndex(Bone);
		if (Parent != INDEX_NONE)
		{
			ParentRotation = ComponentPose.GetComponentSpaceTransform(Parent).GetRotation();
		}

		// BoneCS = Parent * Local, so the local rotation that realises Q * BoneCS is (Parent^-1 * Q * Parent) * Local.
		const FQuat LocalDelta = ParentRotation.Inverse() * ComponentRotation * ParentRotation;
		FTransform& Local = Pose[Bone];
		Local.SetRotation((LocalDelta * Local.GetRotation()).GetNormalized());
	}

	FCompactPoseBoneIndex FindCompactBone(const FBoneContainer& BoneContainer, const FName& BoneName)
	{
		const int32 MeshBoneIndex = BoneContainer.GetPoseBoneIndexForBoneName(BoneName);
		return MeshBoneIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));
	}
}

struct FSWGLayerRootNode : public FAnimNode_Base
{
	FAnimNode_BlendSpacePlayer_Standalone Loop;
	FSWGTimedLayer OneShot;
	FSWGTimedLayer Masked;
	FSWGTimedLayer Additive;

	TObjectPtr<UBlendSpace> LoopSpace;

	// Modifier inputs, copied across at PreUpdate.
	bool bLookAt = false;
	FVector LookAtWorld = FVector::ZeroVector;
	bool bAim = false;
	FVector AimWorld = FVector::ZeroVector;
	FRotator SpineLean = FRotator::ZeroRotator;
	FTransform ComponentTransform = FTransform::Identity;

	// Modifier state carried between frames (radians).
	float HeadYaw = 0.0f;
	float AimPitch = 0.0f;
	float LastDelta = 0.0f;
	FSWGAnimModifierState Applied;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override
	{
		FAnimNode_Base::Initialize_AnyThread(Context);
		Loop.Initialize_AnyThread(Context);
	}

	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override
	{
		FAnimNode_Base::CacheBones_AnyThread(Context);
		Loop.CacheBones_AnyThread(Context);
		OneShot.Player.CacheBones_AnyThread(Context);
		Masked.Player.CacheBones_AnyThread(Context);
		Additive.Player.CacheBones_AnyThread(Context);
		OneShot.WeightsSerial = Masked.WeightsSerial = Additive.WeightsSerial = MAX_uint16;
	}

	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override
	{
		LastDelta = Context.GetDeltaTime();
		Loop.Update_AnyThread(Context);
		OneShot.Update(Context, LastDelta);
		Masked.Update(Context, LastDelta);
		Additive.Update(Context, LastDelta);
	}

	void ApplyLayer(FPoseContext& Output, FSWGTimedLayer& Layer)
	{
		if (!Layer.IsVisible())
		{
			return;
		}

		FPoseContext Over(Output);
		Layer.Player.Evaluate_AnyThread(Over);

		const FBoneContainer& BoneContainer = Output.Pose.GetBoneContainer();
		Layer.EnsureBoneWeights(BoneContainer);

		for (const FCompactPoseBoneIndex BoneIndex : Output.Pose.ForEachBoneIndex())
		{
			const float MaskWeight = Layer.bHasMask ? Layer.BoneWeights[BoneIndex.GetInt()] : 1.0f;
			const float BoneWeight = MaskWeight * Layer.Weight;
			if (BoneWeight <= 0.0f)
			{
				continue;
			}

			FTransform& BaseTransform = Output.Pose[BoneIndex];
			const FTransform& OverTransform = Over.Pose[BoneIndex];
			if (Layer.bAdditive)
			{
				// Overlay deltas are authored around the bind pose: delta = overlay * ref^-1.
				const FTransform& Reference = BoneContainer.GetRefPoseTransform(BoneIndex);
				const FQuat DeltaRotation = OverTransform.GetRotation() * Reference.GetRotation().Inverse();
				const FQuat Scaled = FQuat::Slerp(FQuat::Identity, DeltaRotation, BoneWeight);
				BaseTransform.SetRotation((Scaled * BaseTransform.GetRotation()).GetNormalized());
				if (BoneIndex.GetInt() != 0)
				{
					BaseTransform.AddToTranslation((OverTransform.GetTranslation() - Reference.GetTranslation()) * BoneWeight);
				}
			}
			else
			{
				const FTransform Previous = BaseTransform;
				BaseTransform.Blend(Previous, OverTransform, BoneWeight);
			}
		}
	}

	void ApplyModifiers(FPoseContext& Output)
	{
		Applied = FSWGAnimModifierState();

		const bool bDoAim = bAim && USWGCharacterAnimInstance::bAimEnabled;
		const bool bDoLook = bLookAt && USWGCharacterAnimInstance::bLookAtEnabled;
		const bool bDoLean = !SpineLean.IsNearlyZero();
		if (!bDoAim && !bDoLook && !bDoLean && FMath::IsNearlyZero(HeadYaw) && FMath::IsNearlyZero(AimPitch))
		{
			return;
		}

		const FBoneContainer& BoneContainer = Output.Pose.GetBoneContainer();
		const FVector Up = FVector::UpVector;

		// Aim: tilt the upper body toward the target's elevation.
		const FCompactPoseBoneIndex AimBone = FindCompactBone(BoneContainer, USWGCharacterAnimInstance::AimBoneName);
		if (AimBone != INDEX_NONE && (bDoAim || !FMath::IsNearlyZero(AimPitch)))
		{
			FCSPose<FCompactPose> ComponentPose;
			ComponentPose.InitPose(Output.Pose);

			float DesiredPitch = 0.0f;
			FVector HorizontalDirection = FVector::ForwardVector;
			if (bDoAim)
			{
				const FVector TargetComponent = ComponentTransform.InverseTransformPosition(AimWorld);
				const FVector ToTarget = TargetComponent - ComponentPose.GetComponentSpaceTransform(AimBone).GetLocation();
				const double Horizontal = FMath::Sqrt(ToTarget.X * ToTarget.X + ToTarget.Y * ToTarget.Y);
				DesiredPitch = SWGAnimLayer::ComputeTargetPitch((float)Horizontal, (float)ToTarget.Z, ModifierLimits);
				if (Horizontal > KINDA_SMALL_NUMBER)
				{
					HorizontalDirection = FVector(ToTarget.X, ToTarget.Y, 0.0) / Horizontal;
				}
			}

			const float MaxStep = FMath::DegreesToRadians(ModifierLimits.MaxHeadTurnDegreesPerSecond) * LastDelta;
			AimPitch += FMath::Clamp(DesiredPitch - AimPitch, -MaxStep, MaxStep);

			if (!FMath::IsNearlyZero(AimPitch))
			{
				// Forward x Up is the axis a positive angle lifts forward about.
				const FVector Axis = FVector::CrossProduct(HorizontalDirection, Up).GetSafeNormal();
				RotateBoneInComponentSpace(Output.Pose, ComponentPose, AimBone, FQuat(Axis, AimPitch));
				Applied.bAimApplied = true;
				Applied.AimPitchDegrees = FMath::RadiansToDegrees(AimPitch);
			}
		}

		// Rider spine: component-space lean on the spine base.
		if (bDoLean)
		{
			const FCompactPoseBoneIndex SpineBone = FindCompactBone(BoneContainer, USWGCharacterAnimInstance::RiderSpineBoneName);
			if (SpineBone != INDEX_NONE)
			{
				FCSPose<FCompactPose> ComponentPose;
				ComponentPose.InitPose(Output.Pose);
				RotateBoneInComponentSpace(Output.Pose, ComponentPose, SpineBone, SpineLean.Quaternion());
				Applied.bSpineLeanApplied = true;
			}
		}

		// Look-at: yaw the head toward the target, clamped and rate limited.
		const FCompactPoseBoneIndex HeadBone = FindCompactBone(BoneContainer, USWGCharacterAnimInstance::HeadBoneName);
		if (HeadBone != INDEX_NONE && (bDoLook || !FMath::IsNearlyZero(HeadYaw)))
		{
			FCSPose<FCompactPose> ComponentPose;
			ComponentPose.InitPose(Output.Pose);

			float TargetForward = 1.0f;
			float TargetRight = 0.0f;
			if (bDoLook)
			{
				const FVector TargetComponent = ComponentTransform.InverseTransformPosition(LookAtWorld);
				const FVector ToTarget = TargetComponent - ComponentPose.GetComponentSpaceTransform(HeadBone).GetLocation();
				TargetForward = (float)ToTarget.X;
				TargetRight = (float)ToTarget.Y;
			}

			// With no target the head returns to centre at the same turn speed.
			HeadYaw = bDoLook
				? SWGAnimLayer::StepLookAtYaw(LastDelta, HeadYaw, TargetForward, TargetRight, ModifierLimits)
				: SWGAnimLayer::StepLookAtYaw(LastDelta, HeadYaw, 1.0f, 0.0f, ModifierLimits);

			if (!FMath::IsNearlyZero(HeadYaw))
			{
				RotateBoneInComponentSpace(Output.Pose, ComponentPose, HeadBone, FQuat(Up, HeadYaw));
				Applied.bLookAtApplied = true;
				Applied.HeadYawDegrees = FMath::RadiansToDegrees(HeadYaw);
			}
		}
	}

	virtual void Evaluate_AnyThread(FPoseContext& Output) override
	{
		const bool bOneShotFull = OneShot.IsVisible() && OneShot.Weight >= 0.999f;
		if (!bOneShotFull)
		{
			Loop.Evaluate_AnyThread(Output);
		}

		if (OneShot.IsVisible())
		{
			if (bOneShotFull)
			{
				OneShot.Player.Evaluate_AnyThread(Output);
			}
			else
			{
				FPoseContext Top(Output);
				OneShot.Player.Evaluate_AnyThread(Top);
				FAnimationPoseData BasePose(Output);
				const FAnimationPoseData TopPose(Top);
				FAnimationRuntime::BlendTwoPosesTogetherInPlace(BasePose, TopPose, 1.0f - OneShot.Weight);
			}
		}

		ApplyLayer(Output, Masked);
		ApplyLayer(Output, Additive);
		ApplyModifiers(Output);
	}

	virtual void GatherDebugData(FNodeDebugData& DebugData) override
	{
		DebugData.AddDebugItem(FString::Printf(TEXT("SWG layers one-shot=%d masked=%d add=%d headYaw=%.1f aimPitch=%.1f"),
			OneShot.bActive, Masked.bActive, Additive.bActive, FMath::RadiansToDegrees(HeadYaw), FMath::RadiansToDegrees(AimPitch)));
	}
};

struct FSWGCharacterAnimProxy : public FAnimInstanceProxy
{
	FSWGLayerRootNode Root;

	uint32 AppliedLocomotionSerial = MAX_uint32;
	uint32 AppliedOneShotSerial = 0;
	uint32 AppliedOneShotReleaseSerial = 0;
	uint32 AppliedMaskedSerial = 0;
	uint32 AppliedAdditiveSerial = 0;
	uint32 AppliedMaskedStopSerial = 0;
	uint32 AppliedAdditiveStopSerial = 0;

	FSWGCharacterAnimProxy(UAnimInstance* InInstance)
		: FAnimInstanceProxy(InInstance)
	{
	}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

		USWGCharacterAnimInstance* Instance = Cast<USWGCharacterAnimInstance>(InAnimInstance);
		if (!Instance)
		{
			return;
		}

		if (AppliedLocomotionSerial != Instance->LocomotionSerial)
		{
			AppliedLocomotionSerial = Instance->LocomotionSerial;
			Root.LoopSpace = Instance->LocomotionSpace;
			Root.Loop.SetBlendSpace(Instance->LocomotionSpace);
			Root.Loop.SetPlayRate(Instance->LocomotionPlayRate);
		}
		Root.Loop.SetPosition(FVector(Instance->LocomotionPosition.X, Instance->LocomotionPosition.Y, 0.0));

		if (AppliedOneShotSerial != Instance->OneShotSerial)
		{
			AppliedOneShotSerial = Instance->OneShotSerial;
			Root.OneShot.Start(Instance->OneShotSequence, false, TArray<FName>(), Instance->OneShotPlayRate,
				Instance->OneShotBlendIn, Instance->OneShotBlendOut, Instance->bOneShotHold, Instance->OneShotHoldPosition);
		}
		if (AppliedOneShotReleaseSerial != Instance->OneShotReleaseSerial)
		{
			AppliedOneShotReleaseSerial = Instance->OneShotReleaseSerial;
			Root.OneShot.Stop(Instance->OneShotBlendOut);
		}

		if (AppliedMaskedSerial != Instance->MaskedSerial)
		{
			AppliedMaskedSerial = Instance->MaskedSerial;
			Root.Masked.Start(Instance->MaskedSequence, false, Instance->MaskedBones, Instance->MaskedPlayRate, Instance->MaskedBlendIn, Instance->MaskedBlendOut, false, 0.0f);
		}
		if (AppliedAdditiveSerial != Instance->AdditiveSerial)
		{
			AppliedAdditiveSerial = Instance->AdditiveSerial;
			Root.Additive.Start(Instance->AdditiveSequence, true, Instance->AdditiveBones, Instance->AdditivePlayRate, Instance->AdditiveBlendIn, Instance->AdditiveBlendOut, false, 0.0f);
		}
		if (AppliedMaskedStopSerial != Instance->MaskedStopSerial)
		{
			AppliedMaskedStopSerial = Instance->MaskedStopSerial;
			Root.Masked.Stop(Instance->MaskedBlendOut);
		}
		if (AppliedAdditiveStopSerial != Instance->AdditiveStopSerial)
		{
			AppliedAdditiveStopSerial = Instance->AdditiveStopSerial;
			Root.Additive.Stop(Instance->AdditiveBlendOut);
		}

		Root.bLookAt = Instance->bLookAtActive;
		Root.LookAtWorld = Instance->LookAtTarget;
		Root.bAim = Instance->bAimActive;
		Root.AimWorld = Instance->AimTarget;
		Root.SpineLean = Instance->RiderSpineLean;
		Root.ComponentTransform = GetComponentTransform();
	}

	virtual FAnimNode_Base* GetCustomRootNode() override { return &Root; }

	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override
	{
		OutNodes.Add(&Root);
	}

	virtual void PostEvaluate(UAnimInstance* InAnimInstance) override
	{
		FAnimInstanceProxy::PostEvaluate(InAnimInstance);
		if (USWGCharacterAnimInstance* Instance = Cast<USWGCharacterAnimInstance>(InAnimInstance))
		{
			Instance->bBaseOneShotActive = Root.OneShot.bActive && !Root.OneShot.bHold;
			Instance->bBasePoseHeld = Root.OneShot.bActive && Root.OneShot.bHold;
			Instance->bMaskedOverlayActive = Root.Masked.bActive;
			Instance->bAdditiveOverlayActive = Root.Additive.bActive;
			Instance->ModifierState = Root.Applied;
		}
	}

	virtual void AddReferencedObjects(UAnimInstance* InAnimInstance, FReferenceCollector& Collector) override
	{
		FAnimInstanceProxy::AddReferencedObjects(InAnimInstance, Collector);
		Collector.AddReferencedObject(Root.LoopSpace);
		Collector.AddReferencedObject(Root.OneShot.Sequence);
		Collector.AddReferencedObject(Root.Masked.Sequence);
		Collector.AddReferencedObject(Root.Additive.Sequence);
	}
};

FAnimInstanceProxy* USWGCharacterAnimInstance::CreateAnimInstanceProxy()
{
	return new FSWGCharacterAnimProxy(this);
}

void USWGCharacterAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FSWGCharacterAnimProxy*>(InProxy);
}
