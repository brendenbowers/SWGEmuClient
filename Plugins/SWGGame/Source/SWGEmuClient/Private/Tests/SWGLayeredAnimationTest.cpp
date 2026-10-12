#include "Misc/AutomationTest.h"

#include "Animation/SWGAnimationLayerLogic.h"
#include "Animation/SWGCharacterAnimInstance.h"
#include "Common/SWGLocomotionResolver.h"
#include "Common/SWGPostureTypes.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGAshReader.h"
#include "TRE/SWGLatReader.h"
#include "TRE/SWGMaskReader.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	USWGTreSubsystem* LoadTreSubsystem(FAutomationTestBase& Test)
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
		USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
		if (!Tre)
		{
			Test.AddError(TEXT("could not construct a TRE subsystem"));
			return nullptr;
		}

		Tre->AddToRoot();
		Tre->LoadConfig();
		if (!Tre->LoadArchives())
		{
			Test.AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
			Tre->RemoveFromRoot();
			return nullptr;
		}
		return Tre;
	}
}

// ── Pure selection logic ──────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGLayerLogicTest,
	"SWGEmu.Animation.LayerLogic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGLayerLogicTest::RunTest(const FString& Parameters)
{
	using namespace SWGAnimLayer;

	// DIR codes are a bitfield: right 1, left 2, forward 4, backward 8.
	TestEqual(TEXT("forward-right is forward|right"), (int32)Direction::ForwardRight, (int32)(Direction::Forward | Direction::Right));
	TestEqual(TEXT("backward-left is backward|left"), (int32)Direction::BackwardLeft, (int32)(Direction::Backward | Direction::Left));
	TestEqual(TEXT("right is +90"), DirectionAngleDegrees(Direction::Right), 90.0f, 0.01f);
	TestEqual(TEXT("forward-left is -45"), DirectionAngleDegrees(Direction::ForwardLeft), -45.0f, 0.01f);

	// The ranged stances author every direction except straight back.
	const TArray<uint8> Authored = { Direction::Right, Direction::Left, Direction::Forward, Direction::ForwardRight, Direction::ForwardLeft, Direction::BackwardRight, Direction::BackwardLeft };
	TestEqual(TEXT("running forward picks forward"), (int32)SelectDirection(500.0f, 0.0f, Authored, Direction::Forward), (int32)Direction::Forward);
	TestEqual(TEXT("sidestepping right picks right"), (int32)SelectDirection(0.0f, 400.0f, Authored, Direction::Forward), (int32)Direction::Right);
	TestEqual(TEXT("forward and a little left picks forward-left"), (int32)SelectDirection(300.0f, -250.0f, Authored, Direction::Forward), (int32)Direction::ForwardLeft);
	const uint8 BackSlightRight = SelectDirection(-400.0f, 40.0f, Authored, Direction::Forward);
	TestEqual(TEXT("backpedalling, slightly right, takes the back-right diagonal (no straight-back clip)"), (int32)BackSlightRight, (int32)Direction::BackwardRight);
	TestEqual(TEXT("standing still keeps the previous direction"), (int32)SelectDirection(0.0f, 0.0f, Authored, Direction::Left), (int32)Direction::Left);
	TestEqual(TEXT("with only forward authored everything is forward"), (int32)SelectDirection(-100.0f, 300.0f, TArray<uint8>{ Direction::Forward }, Direction::Forward), (int32)Direction::Forward);
	TestEqual(TEXT("an empty table falls back to forward"), (int32)SelectDirection(100.0f, 0.0f, TArray<uint8>(), Direction::Left), (int32)Direction::Forward);

	// YWAT: negative yaw is a left turn.
	TestTrue(TEXT("a left turn picks YNEG"), SelectYawVariant(-90.0f) == EYawVariant::Negative);
	TestTrue(TEXT("a right turn picks YPOS"), SelectYawVariant(90.0f) == EYawVariant::Positive);
	TestTrue(TEXT("a slow drift picks NONE"), SelectYawVariant(5.0f) == EYawVariant::None);

	TestEqual(TEXT("a TSCL of 1.5 speeds playback"), EffectiveTimeScale(1.5f), 1.5f, 0.0001f);
	TestEqual(TEXT("a zero or negative scale is ignored"), EffectiveTimeScale(0.0f), 1.0f, 0.0001f);

	// LookAtTransformModifier: clamp to +-70 degrees, 600 degrees/second, shortest arc.
	const FLookAtLimits Limits;
	TestEqual(TEXT("retail yaw clamp default"), Limits.YawClampDegrees, 70.0f, 0.001f);
	TestEqual(TEXT("retail head turn speed default"), Limits.MaxHeadTurnDegreesPerSecond, 600.0f, 0.001f);
	TestEqual(TEXT("retail pitch clamp default"), Limits.PitchClampDegrees, 45.0f, 0.001f);

	// 45 degrees to the right, a long frame: reaches it exactly.
	const float AtTarget = StepLookAtYaw(1.0f, 0.0f, 1.0f, 1.0f, Limits);
	TestEqual(TEXT("a target 45 degrees right is reached"), FMath::RadiansToDegrees(AtTarget), 45.0f, 0.01f);
	// Straight to the side: beyond the clamp.
	const float Clamped = StepLookAtYaw(1.0f, 0.0f, 0.0f, 1.0f, Limits);
	TestEqual(TEXT("a target 90 degrees right clamps to 70"), FMath::RadiansToDegrees(Clamped), 70.0f, 0.01f);
	// A short frame only gets as far as the turn speed allows.
	const float Stepped = StepLookAtYaw(0.01f, 0.0f, 0.0f, 1.0f, Limits);
	TestEqual(TEXT("one 10 ms frame turns at most 6 degrees"), FMath::RadiansToDegrees(Stepped), 6.0f, 0.01f);
	// Left works symmetrically.
	TestEqual(TEXT("left turns negative"), FMath::RadiansToDegrees(StepLookAtYaw(1.0f, 0.0f, 0.0f, -1.0f, Limits)), -70.0f, 0.01f);
	// Straight behind: the shortest arc from a head already turned right goes right, not through the front.
	const float Behind = StepLookAtYaw(1.0f, FMath::DegreesToRadians(60.0f), -1.0f, 0.05f, Limits);
	TestTrue(TEXT("a target behind and slightly right keeps the head turned right"), Behind > 0.0f);
	// A target on top of the head relaxes toward centre.
	TestEqual(TEXT("no bearing recentres"), StepLookAtYaw(1.0f, 0.5f, 0.0f, 0.0f, Limits), 0.0f, 0.0001f);

	// TargetPitchTransformModifier: elevation, clamped to +-45.
	TestEqual(TEXT("a target level with the joint needs no pitch"), ComputeTargetPitch(1000.0f, 0.0f, Limits), 0.0f, 0.0001f);
	TestEqual(TEXT("a target 45 degrees up"), FMath::RadiansToDegrees(ComputeTargetPitch(500.0f, 500.0f, Limits)), 45.0f, 0.01f);
	TestEqual(TEXT("steeper clamps to 45"), FMath::RadiansToDegrees(ComputeTargetPitch(100.0f, 900.0f, Limits)), 45.0f, 0.01f);
	TestEqual(TEXT("a target below pitches down"), FMath::RadiansToDegrees(ComputeTargetPitch(500.0f, -250.0f, Limits)), -26.565f, 0.01f);

	// MVAC and AGAT.
	TestTrue(TEXT("a moving creature plays the moving action"), UseMovingAction(300.0f));
	TestFalse(TEXT("a stationary one plays the first action"), UseMovingAction(0.0f));
	TestEqual(TEXT("empty weight table picks nothing"), PickWeighted(TArray<float>(), 0.5f), (int32)INDEX_NONE);
	TestEqual(TEXT("weights pick proportionally (low roll)"), PickWeighted({ 100.0f, 3.0f, 50.0f }, 0.1f), 0);
	TestEqual(TEXT("weights pick proportionally (middle roll)"), PickWeighted({ 100.0f, 3.0f, 50.0f }, 0.66f), 1);
	TestEqual(TEXT("weights pick proportionally (high roll)"), PickWeighted({ 100.0f, 3.0f, 50.0f }, 0.95f), 2);

	// Overlay envelope.
	TestEqual(TEXT("overlay starts at zero"), OverlayWeight(0.0f, 2.0f, 0.2f, 0.4f), 0.0f, 0.0001f);
	TestEqual(TEXT("overlay is full in the middle"), OverlayWeight(1.0f, 2.0f, 0.2f, 0.4f), 1.0f, 0.0001f);
	TestEqual(TEXT("overlay is half out at the half blend-out point"), OverlayWeight(1.8f, 2.0f, 0.2f, 0.4f), 0.5f, 0.0001f);
	TestEqual(TEXT("overlay is gone at its end"), OverlayWeight(2.0f, 2.0f, 0.2f, 0.4f), 0.0f, 0.0001f);
	TestEqual(TEXT("no blend-in starts at full"), OverlayWeight(0.0f, 2.0f, 0.0f, 0.0f), 1.0f, 0.0001f);

	return true;
}

// ── Real .lat / .ash / mask data ──────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGLayeredAnimationDataTest,
	"SWGEmu.Animation.LayeredData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGLayeredAnimationDataTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadTreSubsystem(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	FSWGLatData Lat;
	FSWGAnimationStateHierarchy Hierarchy;
	if (!TestTrue(TEXT("all_m.lat parses"), FSWGLatReader::ReadLat(Tre->CreateIffReader(TEXT("appearance/lat/all_m.lat")), Lat))
		|| !TestTrue(TEXT("all_b.ash parses"), FSWGAshReader::ReadHierarchy(Tre->CreateIffReader(TEXT("appearance/ash/all_b.ash")), Hierarchy)))
	{
		return false;
	}

	// DRAT: seven authored directions, and the entry's own clips are the forward ones.
	const FSWGLatEntry* PistolStand = Lat.Find(TEXT("loop_pistol_combat_standing"));
	if (TestNotNull(TEXT("loop_pistol_combat_standing exists"), PistolStand))
	{
		TestTrue(TEXT("it is directional"), PistolStand->IsDirectional());
		TestEqual(TEXT("seven authored directions"), PistolStand->ClipsByDirection.Num(), 7);
		TestFalse(TEXT("no straight-back branch"), PistolStand->ClipsByDirection.Contains(SWGAnimLayer::Direction::Backward));
		bool bAnyStrafeClip = false;
		for (const FSWGLatClip& Clip : PistolStand->Clips)
		{
			bAnyStrafeClip |= Clip.AnsPath.EndsWith(TEXT("_right.ans")) || Clip.AnsPath.EndsWith(TEXT("_left.ans"));
		}
		TestFalse(TEXT("the default clips are the forward ramp, not a strafe"), bAnyStrafeClip);
		if (const TArray<FSWGLatClip>* Right = PistolStand->ClipsByDirection.Find(SWGAnimLayer::Direction::Right))
		{
			TestTrue(TEXT("DIR 1 is the right strafe"), (*Right)[0].AnsPath.Contains(TEXT("_right")));
		}
		if (const TArray<FSWGLatClip>* BackLeft = PistolStand->ClipsByDirection.Find(SWGAnimLayer::Direction::BackwardLeft))
		{
			TestTrue(TEXT("DIR 10 is back-left"), (*BackLeft)[0].AnsPath.Contains(TEXT("back_left")));
		}
		// The upper-body-masked moving clips carry their mask.
		bool bMaskedMovingClip = false;
		for (const FSWGLatClip& Clip : PistolStand->Clips)
		{
			bMaskedMovingClip |= Clip.ParameterName == TEXT("locomotion") && Clip.MaskPath.EndsWith(TEXT("mask_upper_body.iff"));
		}
		TestTrue(TEXT("moving clips are tagged with the upper-body mask"), bMaskedMovingClip);
	}

	// ... and the resolver turns that into a directional clip set whose Forward ramp is the plain one.
	const int64 InCombat = (int64)1 << (int32)ESWGState::Combat;
	FSWGLocomotionClipSet PistolClips;
	if (TestTrue(TEXT("a pistol stance resolves"), SWGLocomotion::ResolveClipSet(Hierarchy, Lat, ESWGPosture::Upright, InCombat, PistolClips)))
	{
		AddInfo(FString::Printf(TEXT("unarmed combat loop '%s', %d directions"), *PistolClips.IdleLoopName, PistolClips.Directional.Num()));
	}

	// TSCL.
	const FSWGLatEntry* Combo = Lat.Find(TEXT("pole_standing_ready_combo_4b"));
	if (TestNotNull(TEXT("pole_standing_ready_combo_4b exists"), Combo))
	{
		TestEqual(TEXT("its TSCL is 1.5"), Combo->TimeScale, 1.5f, 0.0001f);
		TestEqual(TEXT("and so is its clip's"), Combo->Clips[0].TimeScale, 1.5f, 0.0001f);
	}

	// A masked action clip.
	const FSWGLatEntry* Masked = Lat.Find(TEXT("unarmed_attack_mask"));
	if (TestNotNull(TEXT("unarmed_attack_mask exists"), Masked))
	{
		TestTrue(TEXT("it is masked to the upper body"), Masked->Clips[0].MaskPath.EndsWith(TEXT("mask_upper_body.iff")));
	}

	// AGAT ambient table.
	const FSWGLatEntry* Standing = Lat.Find(TEXT("loop_standing"));
	if (TestNotNull(TEXT("loop_standing exists"), Standing))
	{
		TestTrue(TEXT("loop_standing has an AGAT ambient action table"), Standing->IdleActions.Num() > 0);
		TestTrue(TEXT("with a delay window"), Standing->IdleActionMaxSeconds > Standing->IdleActionMinSeconds && Standing->IdleActionMinSeconds > 0.0f);
		if (Standing->IdleActions.Num() > 0)
		{
			AddInfo(FString::Printf(TEXT("loop_standing idle: %.1f..%.1f s, first action '%s' weight %.1f"),
				Standing->IdleActionMinSeconds, Standing->IdleActionMaxSeconds, *Standing->IdleActions[0].ActionName, Standing->IdleActions[0].Weight));
		}
	}

	// MVAC and useAddTrack in the .ash.
	const FSWGAnimationState* Root = Hierarchy.GetRoot();
	if (TestNotNull(TEXT("ash root"), Root))
	{
		const FSWGAnimationAction* HeavyAttack = Root->ActionDetails.Find(TEXT("attack_heavy"));
		if (TestNotNull(TEXT("root defines attack_heavy"), HeavyAttack))
		{
			TestEqual(TEXT("its stationary action"), HeavyAttack->LogicalName, FString(TEXT("attack_heavy_standing")));
			TestEqual(TEXT("its moving MVAC variant"), HeavyAttack->MovingLogicalName, FString(TEXT("attack_heavy_add")));
			TestTrue(TEXT("the moving variant uses the add track"), HeavyAttack->bMovingUseAddTrack);
			TestFalse(TEXT("the stationary one does not"), HeavyAttack->bUseAddTrack);
		}
		const FSWGAnimationAction* Blink = Root->ActionDetails.Find(TEXT("add_face_blink"));
		if (TestNotNull(TEXT("root defines add_face_blink"), Blink))
		{
			TestTrue(TEXT("a bare ACTN can carry useAddTrack"), Blink->bUseAddTrack);
			TestFalse(TEXT("and has no moving variant"), Blink->HasMovingVariant());
		}
		const FSWGAnimationAction* Plain = Root->ActionDetails.Find(TEXT("adjust"));
		if (TestNotNull(TEXT("root defines adjust"), Plain))
		{
			TestFalse(TEXT("an ordinary emote is full body"), Plain->bUseAddTrack);
		}
	}

	// Resolving an action keeps the layering data.
	if (const FSWGAnimationState* UnarmedCombat = SWGLocomotion::ResolveState(Hierarchy, ESWGPosture::Upright, InCombat))
	{
		int32 MovingVariants = 0;
		int32 AddTrack = 0;
		for (const TPair<FString, FSWGAnimationAction>& Pair : UnarmedCombat->ActionDetails)
		{
			MovingVariants += Pair.Value.HasMovingVariant() ? 1 : 0;
			AddTrack += Pair.Value.bMovingUseAddTrack ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("root/combat: %d actions, %d with a moving variant, %d moving variants on the add track"),
			UnarmedCombat->ActionDetails.Num(), MovingVariants, AddTrack));
		TestTrue(TEXT("unarmed combat has MVAC actions"), MovingVariants > 0);
	}

	// YWAT and AGAT in a creature table.
	FSWGLatData Rancor;
	if (TestTrue(TEXT("rancor.lat parses"), FSWGLatReader::ReadLat(Tre->CreateIffReader(TEXT("appearance/lat/rancor.lat")), Rancor)))
	{
		int32 YawEntries = 0;
		for (const TPair<FString, FSWGLatEntry>& Pair : Rancor.Entries)
		{
			if (Pair.Value.HasYawClips())
			{
				++YawEntries;
				TestTrue(TEXT("YNEG is a left turn"), Pair.Value.YawNegativeClips[0].AnsPath.Contains(TEXT("turn_left")));
				TestTrue(TEXT("YPOS is a right turn"), Pair.Value.YawPositiveClips[0].AnsPath.Contains(TEXT("turn_right")));
				TestFalse(TEXT("the NONE branch is not a turn"), Pair.Value.Clips[0].AnsPath.Contains(TEXT("turn_")));
			}
		}
		AddInfo(FString::Printf(TEXT("rancor.lat: %d entries with turn-in-place clips"), YawEntries));
		TestTrue(TEXT("rancor.lat has turn-in-place entries"), YawEntries > 0);
	}

	// Bone masks.
	TArray<FString> Bones;
	if (TestTrue(TEXT("mask_upper_body.iff parses"), FSWGMaskReader::ReadMask(Tre->CreateIffReader(TEXT("appearance/mask/mask_upper_body.iff")), Bones)))
	{
		TestEqual(TEXT("28 joints"), Bones.Num(), 28);
		TestTrue(TEXT("includes spine3"), Bones.Contains(TEXT("spine3")));
		TestTrue(TEXT("includes the right forearm"), Bones.Contains(TEXT("rforearm")) || Bones.Contains(TEXT("rulna")));
		TestFalse(TEXT("excludes the legs"), Bones.Contains(TEXT("lthigh")));
	}

	return true;
}

// ── The layered anim instance, evaluated for real ─────────────────────────

namespace
{
	/** A saved generated blend space whose skeleton has the joints the modifiers drive, with its mesh. */
	UBlendSpace* FindGeneratedBlendSpace(USkeletalMesh*& OutMesh)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(FPaths::ProjectContentDir() / TEXT("SWGEmu/Generated/BS_*.uasset")), true, false);
		for (const FString& File : Files)
		{
			const FString Name = FPaths::GetBaseFilename(File);
			UBlendSpace* Space = LoadObject<UBlendSpace>(nullptr, *FString::Printf(TEXT("/Game/SWGEmu/Generated/%s.%s"), *Name, *Name));
			if (!Space || !Space->GetSkeleton() || Space->GetBlendSamples().Num() == 0 || !Space->GetBlendSamples()[0].Animation)
			{
				continue;
			}

			const FString SkeletonName = Space->GetSkeleton()->GetName();
			if (!SkeletonName.EndsWith(TEXT("_Skeleton")))
			{
				continue;
			}
			const FString MeshName = SkeletonName.LeftChop(9);
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/SWGEmu/Generated/%s.%s"), *MeshName, *MeshName));
			if (Mesh && Mesh->GetRefSkeleton().FindBoneIndex(USWGCharacterAnimInstance::HeadBoneName) != INDEX_NONE
				&& Mesh->GetRefSkeleton().FindBoneIndex(USWGCharacterAnimInstance::AimBoneName) != INDEX_NONE)
			{
				OutMesh = Mesh;
				return Space;
			}
		}
		return nullptr;
	}

	/** Component-space yaw of Bone relative to the component, degrees. */
	float BoneYawDegrees(USkeletalMeshComponent& Component, const FName& Bone)
	{
		const int32 Index = Component.GetBoneIndex(Bone);
		const TArray<FTransform>& Transforms = Component.GetComponentSpaceTransforms();
		return Transforms.IsValidIndex(Index) ? Transforms[Index].GetRotation().Rotator().Yaw : 0.0f;
	}

	void TickComponent(USkeletalMeshComponent& Component, int32 Frames, float DeltaSeconds)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Component.TickAnimation(DeltaSeconds, false);
			Component.RefreshBoneTransforms();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGLayeredAnimInstanceTest,
	"SWGEmu.Animation.LayeredInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSWGLayeredAnimInstanceTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = nullptr;
	UBlendSpace* Space = FindGeneratedBlendSpace(Mesh);
	if (!Space || !Mesh)
	{
		AddWarning(TEXT("no generated blend space with a matching skeletal mesh under Content/SWGEmu/Generated - run the game once to build one; skipping"));
		return true;
	}
	UAnimSequence* Clip = Cast<UAnimSequence>(Space->GetBlendSamples()[0].Animation);
	if (!Clip)
	{
		AddWarning(TEXT("the blend space's first sample is not an anim sequence; skipping"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("using blend space %s on mesh %s"), *Space->GetName(), *Mesh->GetName()));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SWGLayeredAnimTest"));
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	AActor* Actor = World->SpawnActor<AActor>();
	USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Actor);
	Actor->SetRootComponent(Component);
	Component->SetSkeletalMesh(Mesh);
	Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Component->SetAnimInstanceClass(USWGCharacterAnimInstance::StaticClass());
	Component->RegisterComponent();

	USWGCharacterAnimInstance* Instance = Cast<USWGCharacterAnimInstance>(Component->GetAnimInstance());
	if (!TestNotNull(TEXT("layered anim instance is created"), Instance))
	{
		return false;
	}

	Instance->SetLocomotion(Space);
	Instance->SetLocomotionInput(FVector2D(0.0f, 0.0f));
	TickComponent(*Component, 3, 0.033f);
	TestTrue(TEXT("the loop produces a pose"), Component->GetComponentSpaceTransforms().Num() > 0);

	// Head look-at: baseline, then a target well to the right, then back off.
	const float HeadBaseline = BoneYawDegrees(*Component, USWGCharacterAnimInstance::HeadBoneName);
	const FVector HeadLocation = Component->GetBoneLocation(USWGCharacterAnimInstance::HeadBoneName, EBoneSpaces::WorldSpace);
	Instance->SetLookAtTarget(true, HeadLocation + FVector(0.0, 500.0, 0.0));
	TickComponent(*Component, 12, 0.033f);
	const float HeadTurned = BoneYawDegrees(*Component, USWGCharacterAnimInstance::HeadBoneName);
	AddInfo(FString::Printf(TEXT("head yaw %.1f -> %.1f, modifier says %.1f"), HeadBaseline, HeadTurned, Instance->GetModifierState().HeadYawDegrees));
	TestTrue(TEXT("the head turned right toward a target at +Y"), FMath::FindDeltaAngleDegrees(HeadBaseline, HeadTurned) > 40.0f);
	TestTrue(TEXT("the turn stops at the 70 degree clamp"), FMath::FindDeltaAngleDegrees(HeadBaseline, HeadTurned) < 75.0f);
	TestTrue(TEXT("the modifier reports it"), Instance->GetModifierState().bLookAtApplied && Instance->GetModifierState().HeadYawDegrees > 40.0f);

	USWGCharacterAnimInstance::bLookAtEnabled = false;
	TickComponent(*Component, 20, 0.033f);
	const float HeadOff = BoneYawDegrees(*Component, USWGCharacterAnimInstance::HeadBoneName);
	USWGCharacterAnimInstance::bLookAtEnabled = true;
	TestTrue(TEXT("with look-at off the head returns to its animated yaw"), FMath::Abs(FMath::FindDeltaAngleDegrees(HeadBaseline, HeadOff)) < 5.0f);

	Instance->SetLookAtTarget(false, FVector::ZeroVector);
	TickComponent(*Component, 20, 0.033f);

	// Aim pitch: a target high above the spine pitches the upper body.
	const FVector SpineLocation = Component->GetBoneLocation(USWGCharacterAnimInstance::AimBoneName, EBoneSpaces::WorldSpace);
	Instance->SetAimTarget(true, SpineLocation + FVector(500.0, 0.0, 500.0));
	TickComponent(*Component, 20, 0.033f);
	TestTrue(TEXT("aim pitch applied"), Instance->GetModifierState().bAimApplied);
	TestEqual(TEXT("a 45 degree elevation pitches 45 degrees"), Instance->GetModifierState().AimPitchDegrees, 45.0f, 1.0f);
	Instance->SetAimTarget(false, FVector::ZeroVector);
	TickComponent(*Component, 20, 0.033f);
	TestFalse(TEXT("aim pitch relaxes when the target goes"), Instance->GetModifierState().bAimApplied);

	// Action track: a one-shot plays over the loop and falls back to it.
	Instance->PlayBaseOneShot(Clip, 1.0f, 0.0f, 0.1f);
	TickComponent(*Component, 2, 0.033f);
	TestTrue(TEXT("the one-shot is active"), Instance->IsBaseOneShotActive());
	TickComponent(*Component, FMath::CeilToInt(Clip->GetPlayLength() / 0.033f) + 4, 0.033f);
	TestFalse(TEXT("and finishes after its length"), Instance->IsBaseOneShotActive());

	// Masked and additive overlays run independently and finish.
	const TArray<FName> UpperBody = { FName(TEXT("spine1")), FName(TEXT("spine2")), FName(TEXT("spine3")), FName(TEXT("neck")), FName(TEXT("head")) };
	Instance->PlayOverlay(Clip, false, UpperBody, 1.0f, 0.05f, 0.1f);
	Instance->PlayOverlay(Clip, true, UpperBody, 1.0f, 0.05f, 0.1f);
	TickComponent(*Component, 3, 0.033f);
	TestTrue(TEXT("masked overlay active"), Instance->IsOverlayActive(false));
	TestTrue(TEXT("additive overlay active"), Instance->IsOverlayActive(true));
	TestFalse(TEXT("overlays leave the action track alone"), Instance->IsBaseOneShotActive());
	TickComponent(*Component, FMath::CeilToInt(Clip->GetPlayLength() / 0.033f) + 4, 0.033f);
	TestFalse(TEXT("masked overlay finishes"), Instance->IsOverlayActive(false));
	TestFalse(TEXT("additive overlay finishes"), Instance->IsOverlayActive(true));

	// Held pose (the datapad), then released.
	Instance->HoldBasePose(Clip, 0.5f);
	TickComponent(*Component, 5, 0.033f);
	TestTrue(TEXT("pose held"), Instance->IsBasePoseHeld());
	TickComponent(*Component, 60, 0.033f);
	TestTrue(TEXT("a held pose does not end by itself"), Instance->IsBasePoseHeld());
	Instance->ReleaseBasePose(0.1f);
	TickComponent(*Component, 10, 0.033f);
	TestFalse(TEXT("released"), Instance->IsBasePoseHeld());

	Component->DestroyComponent();
	return true;
}

#endif
