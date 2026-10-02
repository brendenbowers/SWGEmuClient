#include "SWGPlacementDatapadMode.h"
#include "SWGStructureHoloDroid.h"
#include "SWGCameraTakeover.h"
#include "Subsystems/SWGStructurePlacementSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "Structure/SWGPlacementRules.h"
#include "Blueprint/UserWidget.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TwoBoneIK.h"
#include "UObject/ConstructorHelpers.h"

USWGPlacementDatapadMode::USWGPlacementDatapadMode()
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ScreenMaterial(TEXT("/Game/SWGEmu/Materials/M_SWGDatapadCamera.M_SWGDatapadCamera"));
	DatapadBaseMaterial = ScreenMaterial.Object;
}

bool USWGPlacementDatapadMode::Begin(UUserWidget* InOwner, FSWGCameraTakeover* InView)
{
	if (!InOwner || !InView || bActive || !DatapadBaseMaterial) { return false; }
	UWorld* World = InOwner->GetWorld();
	APawn* Pawn = InOwner->GetOwningPlayerPawn();
	UGameInstance* GameInstance = InOwner->GetGameInstance();
	Placement = GameInstance ? GameInstance->GetSubsystem<USWGStructurePlacementSubsystem>() : nullptr;
	Terrain = GameInstance ? GameInstance->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
	if (!World || !Pawn || !Terrain || !Placement) { return false; }
	Owner = InOwner;
	View = InView;
	PlayerOrigin = Pawn->GetActorLocation();

	DatapadTarget = NewObject<UTextureRenderTarget2D>(this);
	DatapadTarget->ClearColor = FLinearColor::Black;
	DatapadTarget->InitCustomFormat(960, 540, PF_B8G8R8A8, false);
	DatapadTarget->UpdateResourceImmediate(true);
	DatapadMaterial = UMaterialInstanceDynamic::Create(DatapadBaseMaterial, this);
	DatapadMaterial->SetTextureParameterValue(TEXT("CameraFeed"), DatapadTarget);
	auto AddScreen = [this](UStaticMeshComponent* Body, float Scale)
	{
		UStaticMeshComponent* Display = NewObject<UStaticMeshComponent>(Body->GetOwner());
		Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Display->SetCastShadow(false);
		Display->SetVisibility(false);
		Display->SetupAttachment(Body);
		Display->SetUsingAbsoluteScale(true);
		Display->SetRelativeLocation(FVector(0.f, 0.f, 66.f));
		Display->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
		Display->SetRelativeScale3D(FVector(.20f * Scale, .116f * Scale, 1.f));
		Display->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
		Display->SetMaterial(0, DatapadMaterial);
		Display->RegisterComponent();
		return Display;
	};
	DroneCapture = World->SpawnActor<ASceneCapture2D>();
	ProjectorDroid = World->SpawnActor<ASWGStructureHoloDroid>();
	CameraDroid = World->SpawnActor<ASWGStructureHoloDroid>();
	if (!DroneCapture || !ProjectorDroid || !CameraDroid)
	{
		End();
		return false;
	}
	DroneCapture->GetCaptureComponent2D()->TextureTarget = DatapadTarget;
	DroneCapture->GetCaptureComponent2D()->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	DroneCapture->GetCaptureComponent2D()->FOVAngle = 65.f;
	CameraDroid->DiscDiameter = 160.f;
	CameraDroid->DroidHeight = 0.f;
	CameraDroid->DroidReach = 0.f;
	CameraDroid->SetProjectionSurfaceVisible(false);
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (USkeletalMeshComponent* Body = Character->GetMesh())
		{
			HeldDatapad = NewObject<UStaticMeshComponent>(Character);
			HeldDatapad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			HeldDatapad->SetCastShadow(false);
			HeldDatapad->SetVisibility(false);
			HeldDatapad->SetupAttachment(Character->GetRootComponent());
			HeldDatapad->RegisterComponent();
			HeldDatapad->SetRelativeRotation(FRotator(19.3f, 0.f, 0.f));
			PoseMesh = Body;
			PoseHandle = Body->RegisterOnBoneTransformsFinalizedDelegate(
				FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &USWGPlacementDatapadMode::PoseHands, Body));
			DatapadDisplay = AddScreen(HeldDatapad, 2.2f);
		}
	}
	const FVector PlayerRaw = SWGToRawSpace(Pawn->GetActorLocation());
	if (FVector2D::Distance(Placement->GetCandidate(), FVector2D(PlayerRaw.X, PlayerRaw.Y)) < 5.f)
	{
		const FVector Ahead = SWGToRawSpace(Pawn->GetActorLocation() + Pawn->GetActorForwardVector().GetSafeNormal2D() * 3000.f);
		Placement->SetCandidate(FVector2D(Ahead.X, Ahead.Y));
	}
	const FVector CandidateWorld = SWGToUnrealSpace(FVector(Placement->GetCandidate().X, Placement->GetCandidate().Y, 0.f));
	const FVector CameraWorld = CandidateWorld - Pawn->GetActorForwardVector().GetSafeNormal2D() * 5000.f + Pawn->GetActorRightVector().GetSafeNormal2D() * 1600.f;
	const FVector Raw = SWGToRawSpace(CameraWorld);
	CameraRaw = FVector2D(Raw.X, Raw.Y);
	bActive = true;
	bMovingCameraDroid = false;
	UpdateScene();
	UpdateCamera();
	UStaticMesh* PadMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* PadBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (PadMesh && PadBase && HeldDatapad)
	{
		UMaterialInstanceDynamic* PadMaterial = UMaterialInstanceDynamic::Create(PadBase, this);
		PadMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(.18f, .22f, .27f));
		HeldDatapad->SetStaticMesh(PadMesh);
		HeldDatapad->SetRelativeScale3D(FVector(.125f, .21f, .0035f) * 2.2f);
		HeldDatapad->SetMaterial(0, PadMaterial);
		HeldDatapad->SetVisibility(true);
		if (DatapadDisplay) { DatapadDisplay->SetVisibility(true); }
	}
	OnStateChanged.Broadcast();
	return true;
}

void USWGPlacementDatapadMode::End()
{
	if (PoseMesh.IsValid()) { PoseMesh->UnregisterOnBoneTransformsFinalizedDelegate(PoseHandle); PoseMesh.Reset(); }
	const bool bWasActive = bActive;
	bActive = false;
	bDragging = false;
	HeldKeys.Empty();
	if (DroneCapture) { DroneCapture->Destroy(); DroneCapture = nullptr; }
	if (ProjectorDroid) { ProjectorDroid->Destroy(); ProjectorDroid = nullptr; }
	if (CameraDroid) { CameraDroid->Destroy(); CameraDroid = nullptr; }
	if (DatapadDisplay) { DatapadDisplay->DestroyComponent(); DatapadDisplay = nullptr; }
	if (HeldDatapad) { HeldDatapad->DestroyComponent(); HeldDatapad = nullptr; }
	DatapadTarget = nullptr;
	DatapadMaterial = nullptr;
	View = nullptr;
	Owner = nullptr;
	if (bWasActive) { OnStateChanged.Broadcast(); }
}

void USWGPlacementDatapadMode::SwitchDroid()
{
	bMovingCameraDroid = !bMovingCameraDroid;
	OnStateChanged.Broadcast();
}

void USWGPlacementDatapadMode::Tick(float DeltaTime)
{
	const APlayerController* PC = Owner ? Owner->GetOwningPlayer() : nullptr;
	if (!bActive || !PC) { return; }
	// WASD drives the selected droid and the arrows drive the other one, so the camera and the structure
	// can be moved together; both move along the camera droid's forward and right.
	auto IsHeld = [this, PC](const FKey& Key) { return HeldKeys.Contains(Key) || PC->IsInputKeyDown(Key); };
	FVector2D Selected(0.f, 0.f), Other(0.f, 0.f);
	if (IsHeld(EKeys::W)) { Selected.Y += 1.f; }
	if (IsHeld(EKeys::S)) { Selected.Y -= 1.f; }
	if (IsHeld(EKeys::D)) { Selected.X += 1.f; }
	if (IsHeld(EKeys::A)) { Selected.X -= 1.f; }
	if (IsHeld(EKeys::Up)) { Other.Y += 1.f; }
	if (IsHeld(EKeys::Down)) { Other.Y -= 1.f; }
	if (IsHeld(EKeys::Right)) { Other.X += 1.f; }
	if (IsHeld(EKeys::Left)) { Other.X -= 1.f; }
	if (!Selected.IsNearlyZero()) { MoveDroid(Selected.GetSafeNormal(), DeltaTime, bMovingCameraDroid); }
	if (!Other.IsNearlyZero()) { MoveDroid(Other.GetSafeNormal(), DeltaTime, !bMovingCameraDroid); }
	if (IsHeld(EKeys::E)) { CameraHeight = FMath::Clamp(CameraHeight + 900.f * DeltaTime, 500.f, 6000.f); }
	if (IsHeld(EKeys::Q)) { CameraHeight = FMath::Clamp(CameraHeight - 900.f * DeltaTime, 500.f, 6000.f); }
	UpdateScene();
	UpdateCamera();
}

bool USWGPlacementDatapadMode::HandleKeyDown(const FKey& Key, bool bShift, bool bControl)
{
	if (!bActive) { return false; }
	if (Key == EKeys::Tab) { SwitchDroid(); return true; }
	if (Key == EKeys::W || Key == EKeys::A || Key == EKeys::S || Key == EKeys::D || Key == EKeys::Up || Key == EKeys::Down
		|| Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Q || Key == EKeys::E)
	{
		HeldKeys.Add(Key);
		return true;
	}
	return false;
}

bool USWGPlacementDatapadMode::HandleKeyUp(const FKey& Key)
{
	HeldKeys.Remove(Key);
	return false;
}

bool USWGPlacementDatapadMode::HandleMouseButtonDown(const FKey& Button)
{
	if (!bActive || !Placement) { return false; }
	if (Button == EKeys::RightMouseButton) { Placement->Rotate(1); }
	else if (Button == EKeys::MiddleMouseButton) { SwitchDroid(); }
	else if (Button == EKeys::LeftMouseButton) { bDragging = true; }
	return true;
}

bool USWGPlacementDatapadMode::HandleMouseButtonUp(const FKey& Button)
{
	if (Button != EKeys::LeftMouseButton || !bDragging) { return false; }
	bDragging = false;
	return true;
}

bool USWGPlacementDatapadMode::HandleMouseMove(const FVector2D& CursorDelta)
{
	if (!bActive || !bDragging) { return false; }
	if (!CursorDelta.IsNearlyZero()) { MoveDroid(FVector2D(CursorDelta.X, -CursorDelta.Y), 1.f / 180.f, bMovingCameraDroid); }
	return true;
}

bool USWGPlacementDatapadMode::HandleMouseWheel(float Delta)
{
	if (!bActive) { return false; }
	CameraHeight = FMath::Clamp(CameraHeight - Delta * 200.f, 500.f, 6000.f);
	UpdateScene();
	return true;
}

void USWGPlacementDatapadMode::HandleFocusLost()
{
	HeldKeys.Empty();
	bDragging = false;
}

void USWGPlacementDatapadMode::MoveDroid(FVector2D Direction, float DeltaTime, bool bCameraDroid)
{
	const APawn* Pawn = Owner ? Owner->GetOwningPlayerPawn() : nullptr;
	if (!Pawn || !Placement) { return; }
	const FVector Forward = DroneCapture ? DroneCapture->GetActorForwardVector().GetSafeNormal2D() : Pawn->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = DroneCapture ? DroneCapture->GetActorRightVector().GetSafeNormal2D() : Pawn->GetActorRightVector().GetSafeNormal2D();
	const FVector Motion = (Forward * Direction.Y + Right * Direction.X) * 18.f * DeltaTime * SWGWorldScale;
	const FVector RawMotion = SWGToRawSpace(Motion);
	const FVector2D Step(RawMotion.X, RawMotion.Y);
	if (bCameraDroid)
	{
		const FVector PlayerRaw = SWGToRawSpace(PlayerOrigin);
		CameraRaw = FVector2D(PlayerRaw.X, PlayerRaw.Y) + (CameraRaw + Step - FVector2D(PlayerRaw.X, PlayerRaw.Y)).GetClampedToMaxSize(100.f);
	}
	else { Placement->SetCandidate(Placement->GetCandidate() + Step); }
	UpdateScene();
}

void USWGPlacementDatapadMode::UpdateScene()
{
	if (!bActive || !Terrain || !Placement || !ProjectorDroid || !CameraDroid || !DroneCapture) { return; }
	const FVector2D Candidate = Placement->GetCandidate();
	const FVector Ground = SWGToUnrealSpace(FVector(Candidate.X, Candidate.Y, Terrain->GetHeightAt(Candidate.X, Candidate.Y)));
	ProjectorDroid->SetActorLocation(Ground + FVector(0.f, 0.f, 25.f));
	const FVector CameraGround = SWGToUnrealSpace(FVector(CameraRaw.X, CameraRaw.Y, Terrain->GetHeightAt(CameraRaw.X, CameraRaw.Y)));
	const FVector CameraPosition = CameraGround + FVector(0.f, 0.f, CameraHeight);
	CameraDroid->SetActorLocation(CameraPosition);
	const FVector ToCamera = (CameraPosition - Ground).GetSafeNormal2D();
	ProjectorDroid->SetDroidSide(FVector2D(ToCamera.X, ToCamera.Y));
	const FVector Target = Ground + FVector(0.f, 0.f, 300.f);
	const FVector Direction = (Target - CameraPosition).GetSafeNormal();
	DroneCapture->SetActorLocationAndRotation(CameraPosition + Direction * 100.f, Direction.Rotation());
	// The projector droid beams the hologram: rays from its lens to the footprint corners and above the centre.
	const FSWGStructureFootprint& Footprint = Placement->GetFootprint();
	const FBox2D Local = Footprint.IsValid() ? FSWGPlacementRules::GetServerFootprintRect(Footprint, Placement->GetRotation() * 90) : FBox2D(FVector2D(-4.f, -4.f), FVector2D(4.f, 4.f));
	const FVector2D Corners[] = { Local.Min, FVector2D(Local.Max.X, Local.Min.Y), Local.Max, FVector2D(Local.Min.X, Local.Max.Y) };
	TArray<FVector> RayTargets;
	TArray<float> RayBrightness;
	for (const FVector2D& Corner : Corners)
	{
		const FVector2D World = Candidate + Corner;
		RayTargets.Add(SWGToUnrealSpace(FVector(World.X, World.Y, Terrain->GetHeightAt(World.X, World.Y) + .2f)));
		RayBrightness.Add(.5f);
	}
	RayTargets.Add(Ground + FVector(0.f, 0.f, 900.f));
	RayBrightness.Add(.7f);
	ProjectorDroid->SetExtraRays(RayTargets, RayBrightness);
}

void USWGPlacementDatapadMode::UpdateCamera()
{
	const APawn* Pawn = Owner ? Owner->GetOwningPlayerPawn() : nullptr;
	if (!bActive || !Pawn || !View) { return; }
	FVector Eye;
	FRotator EyeRotation;
	Pawn->GetActorEyesViewPoint(Eye, EyeRotation);
	const FVector Forward = Pawn->GetActorForwardVector().GetSafeNormal2D();
	const FVector PadCenter = Eye + Forward * 40.f - FVector(0.f, 0.f, 35.f);
	const FVector Camera = PadCenter - Forward * 85.f + FVector(0.f, 0.f, 200.f);
	if (HeldDatapad) { HeldDatapad->SetWorldLocation(PadCenter); }
	if (!View->GetCamera()) { View->Begin(*Owner, Camera, PadCenter, 28.f, .35f, 0.f); }
	else { View->Retarget(Camera, PadCenter, 28.f); }
}

void USWGPlacementDatapadMode::PoseHands(USkeletalMeshComponent* Body)
{
	if (!bActive || !HeldDatapad || !Body || !Body->GetSkeletalMeshAsset()) { return; }
	TArray<FTransform>& Bones = Body->GetEditableComponentSpaceTransforms();
	const FReferenceSkeleton& Skeleton = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
	const FTransform MeshWorld = Body->GetComponentTransform();
	const FVector Center = HeldDatapad->GetComponentLocation();
	const FVector Front = HeldDatapad->GetForwardVector();
	const FVector Side = HeldDatapad->GetRightVector();
	const FVector Normal = HeldDatapad->GetUpVector();
	auto RotateBranch = [&Bones, &Skeleton](int32 Root, const FVector& Pivot, const FQuat& Rotation)
	{
		for (int32 Index = Root; Index < Bones.Num(); ++Index)
		{
			if (Index != Root && !Skeleton.BoneIsChildOf(Index, Root)) { continue; }
			Bones[Index].SetLocation(Pivot + Rotation.RotateVector(Bones[Index].GetLocation() - Pivot));
			Bones[Index].SetRotation((Rotation * Bones[Index].GetRotation()).GetNormalized());
		}
	};
	auto PlaceHand = [&](const TCHAR* ArmName, const TCHAR* ForearmName, const TCHAR* HandName, float Sign)
	{
		const int32 Arm = Body->GetBoneIndex(ArmName);
		const int32 Forearm = Body->GetBoneIndex(ForearmName);
		const int32 Hand = Body->GetBoneIndex(HandName);
		if (Arm == INDEX_NONE || Forearm == INDEX_NONE || Hand == INDEX_NONE || Hand >= Bones.Num()
			|| !Skeleton.BoneIsChildOf(Forearm, Arm) || !Skeleton.BoneIsChildOf(Hand, Forearm)) { return; }
		const FVector Shoulder = Bones[Arm].GetLocation();
		const FVector Elbow = Bones[Forearm].GetLocation();
		const FVector Wrist = Bones[Hand].GetLocation();
		const FVector Target = MeshWorld.InverseTransformPosition(Center - Front * 9.f + Side * (Sign * 21.f) + Normal * 2.f);
		FVector SolvedElbow, SolvedWrist;
		AnimationCore::SolveTwoBoneIK(Shoulder, Elbow, Wrist, Elbow, Target,
			SolvedElbow, SolvedWrist, false, 1.0, 1.0);
		RotateBranch(Arm, Shoulder, FQuat::FindBetweenNormals((Elbow - Shoulder).GetSafeNormal(), (SolvedElbow - Shoulder).GetSafeNormal()));
		const FVector MovedElbow = Bones[Forearm].GetLocation();
		const FVector MovedWrist = Bones[Hand].GetLocation();
		RotateBranch(Forearm, MovedElbow, FQuat::FindBetweenNormals((MovedWrist - MovedElbow).GetSafeNormal(), (SolvedWrist - SolvedElbow).GetSafeNormal()));
	};
	PlaceHand(TEXT("lArm"), TEXT("lForeArm"), TEXT("lWrist"), -1.f);
	PlaceHand(TEXT("rArm"), TEXT("rForeArm"), TEXT("rWrist"), 1.f);
	Body->MarkRenderDynamicDataDirty();
}
