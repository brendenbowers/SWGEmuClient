#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "SWGCharacterAnimInstance.generated.h"

class UAnimSequence;
class UBlendSpace;

/** What the head/aim/rider modifiers did on the last evaluation, for the console and tests. */
struct FSWGAnimModifierState
{
	bool bLookAtApplied = false;
	float HeadYawDegrees = 0.0f;
	bool bAimApplied = false;
	float AimPitchDegrees = 0.0f;
	bool bSpineLeanApplied = false;
};

/**
 * Layered character animation, built in C++ with no animation blueprint. It
 * mirrors the retail StateHierarchyAnimationController's three tracks
 * (SWGEmu.exe 0x7cf390: "loop", "action", "add"):
 *
 *  - loop: the state's locomotion blend space (or a 2D variant steered by yaw
 *    rate for turn-in-place, or by movement direction for strafing);
 *  - action: one-shot clips. Played full body over the loop, or restricted to a
 *    bone mask (the LAT's PPTR masks) so the legs keep walking;
 *  - add: additive overlay clips (useAddTrack actions), applied onto the pose
 *    below with the same optional bone mask.
 *
 * Head look-at, aim pitch and rider spine run last as bone rotations (see
 * Docs/RetailAnimationNotes.md).
 *
 * Everything public here is game-thread; the anim proxy copies it across at
 * PreUpdate, so changes land on the next animation tick.
 */
UCLASS(Transient)
class SWGEMUCLIENT_API USWGCharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** The loop track. PlayRate scales the blend space (the LAT's TSCL). Null clears it (reference pose). */
	void SetLocomotion(UBlendSpace* BlendSpace, float PlayRate = 1.0f);

	/** Blend space coordinates: X horizontal speed, Y the steering axis of 2D spaces (yaw rate in -1..1, or movement heading in degrees). */
	void SetLocomotionInput(const FVector2D& Position);

	/** The action track, full body. Blends out over BlendOut seconds at the end and falls back to the loop. */
	void PlayBaseOneShot(UAnimSequence* Sequence, float PlayRate = 1.0f, float BlendIn = 0.05f, float BlendOut = 0.15f);

	/** Freezes the action track on one frame, e.g. the datapad pose; ReleaseBasePose returns to the loop. */
	void HoldBasePose(UAnimSequence* Sequence, float NormalizedPosition);
	void ReleaseBasePose(float BlendOut = 0.15f);

	/**
	 * The add track when bAdditive, otherwise the masked action track. MaskBones
	 * limits the clip to those joints (empty: the whole body). Additive clips are
	 * deltas against the reference pose, applied onto whatever is below.
	 */
	void PlayOverlay(UAnimSequence* Sequence, bool bAdditive, const TArray<FName>& MaskBones, float PlayRate = 1.0f, float BlendIn = 0.1f, float BlendOut = 0.15f);
	void StopOverlay(bool bAdditive);

	bool IsBaseOneShotActive() const { return bBaseOneShotActive; }
	bool IsBasePoseHeld() const { return bBasePoseHeld; }
	bool IsOverlayActive(bool bAdditive) const { return bAdditive ? bAdditiveOverlayActive : bMaskedOverlayActive; }
	bool IsAnyActionActive() const { return bBaseOneShotActive || bMaskedOverlayActive || bAdditiveOverlayActive; }

	/** Head turns toward WorldTarget (clamped and rate limited like retail's LookAtTransformModifier). */
	void SetLookAtTarget(bool bActive, const FVector& WorldTarget);

	/** Upper body pitches toward WorldTarget's elevation (retail's TargetPitchTransformModifier). */
	void SetAimTarget(bool bActive, const FVector& WorldTarget);

	/** Component-space lean (yaw, pitch, roll in degrees) added to the rider's spine; zero clears it. */
	void SetRiderSpineLean(const FRotator& ComponentSpaceLean);

	const FSWGAnimModifierState& GetModifierState() const { return ModifierState; }

	/** swg.Anim.LookAt / swg.Anim.Aim: the modifiers can be switched off globally to compare against the plain pose. */
	static bool bLookAtEnabled;
	static bool bAimEnabled;

	// Joints the modifiers rotate, by name in the SWG skeleton (the exe's name table at 0x1869c24 lists spine3, neck, head, spine1).
	static const FName HeadBoneName;
	static const FName AimBoneName;
	static const FName RiderSpineBoneName;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	friend struct FSWGCharacterAnimProxy;

	UPROPERTY(Transient)
	TObjectPtr<UBlendSpace> LocomotionSpace;
	float LocomotionPlayRate = 1.0f;
	FVector2D LocomotionPosition = FVector2D::ZeroVector;
	uint32 LocomotionSerial = 0;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> OneShotSequence;
	float OneShotPlayRate = 1.0f;
	float OneShotBlendIn = 0.05f;
	float OneShotBlendOut = 0.15f;
	float OneShotHoldPosition = 0.0f;
	bool bOneShotHold = false;
	uint32 OneShotSerial = 0;
	uint32 OneShotReleaseSerial = 0;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> MaskedSequence;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> AdditiveSequence;
	TArray<FName> MaskedBones;
	TArray<FName> AdditiveBones;
	float MaskedPlayRate = 1.0f;
	float AdditivePlayRate = 1.0f;
	float MaskedBlendIn = 0.1f;
	float AdditiveBlendIn = 0.1f;
	float MaskedBlendOut = 0.15f;
	float AdditiveBlendOut = 0.15f;
	uint32 MaskedSerial = 0;
	uint32 AdditiveSerial = 0;
	uint32 MaskedStopSerial = 0;
	uint32 AdditiveStopSerial = 0;

	bool bLookAtActive = false;
	FVector LookAtTarget = FVector::ZeroVector;
	bool bAimActive = false;
	FVector AimTarget = FVector::ZeroVector;
	FRotator RiderSpineLean = FRotator::ZeroRotator;

	// Written back by the proxy after each update.
	bool bBaseOneShotActive = false;
	bool bBasePoseHeld = false;
	bool bMaskedOverlayActive = false;
	bool bAdditiveOverlayActive = false;
	FSWGAnimModifierState ModifierState;
};
