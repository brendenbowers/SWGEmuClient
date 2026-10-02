#include "SWGStructurePlacementWidget.h"
#include "SWGStructureHoloDroid.h"
#include "SWGPlacementMapMode.h"
#include "Subsystems/SWGStructurePlacementSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "Structure/SWGPlacementRules.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "TwoBoneIK.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Camera/CameraActor.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ConstructorHelpers.h"

USWGStructurePlacementWidget::USWGStructurePlacementWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ScreenMaterial(TEXT("/Game/SWGEmu/Materials/M_SWGDatapadCamera.M_SWGDatapadCamera"));
	DatapadBaseMaterial = ScreenMaterial.Object;
}

namespace
{
	constexpr float PlacementPitchDegrees = 75.f, PlacementFovDegrees = 70.f, PlacementRangeMeters = 100.f;

	/** Camera height (cm) at which the server's placement range circle, plus a 10% margin, just fits the viewport's vertical span. */
	float MaxPlacementHeight(const APlayerController* PC)
	{
		int32 Width = 0, Height = 0;
		if (PC) { PC->GetViewportSize(Width, Height); }
		const float Aspect = Width > 0 && Height > 0 ? static_cast<float>(Width) / Height : 16.f / 9.f;
		const float HalfVertical = FMath::Atan(FMath::Tan(FMath::DegreesToRadians(PlacementFovDegrees * .5f)) / Aspect);
		const float Pitch = FMath::DegreesToRadians(PlacementPitchDegrees);
		const float SpanPerHeight = 1.f / FMath::Tan(Pitch - HalfVertical) - 1.f / FMath::Tan(Pitch + HalfVertical);
		return PlacementRangeMeters * 2.f * 1.1f * 100.f / SpanPerHeight;
	}
}

void USWGStructurePlacementWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	// Canvas panels are not hit-testable by default; without this, clicks on empty screen never reach the mouse handlers below.
	Canvas->SetVisibility(ESlateVisibility::Visible);
	WidgetTree->RootWidget = Canvas;
	PlacementPanel = WidgetTree->ConstructWidget<UVerticalBox>();
	UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(PlacementPanel);
	PanelSlot->SetAnchors(FAnchors(0.f, 0.f));
	PanelSlot->SetAlignment(FVector2D::ZeroVector);
	PanelSlot->SetAutoSize(true);
	PanelSlot->SetPosition(FVector2D(12.f, 12.f));
	StatusText = WidgetTree->ConstructWidget<UTextBlock>();
	StatusText->SetText(FText::FromString(TEXT("Structure placement")));
	StatusText->SetWrapTextAt(880.f);
	PlacementPanel->AddChildToVerticalBox(StatusText);
	PromptBorder = WidgetTree->ConstructWidget<UBorder>();
	PromptBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, .7f));
	PromptBorder->SetPadding(FMargin(4.f, 2.f));
	PromptBorder->SetVisibility(ESlateVisibility::Collapsed);
	PlacementPanel->AddChildToVerticalBox(PromptBorder);
	PromptText = WidgetTree->ConstructWidget<UTextBlock>();
	FSlateFontInfo PromptFont = StatusText->GetFont();
	PromptFont.Size = 13;
	PromptText->SetFont(PromptFont);
	PromptBorder->AddChild(PromptText);
}

void USWGStructurePlacementWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Placement = GetGameInstance()->GetSubsystem<USWGStructurePlacementSubsystem>();
	Terrain = GetGameInstance()->GetSubsystem<USWGTerrainSubsystem>();
	if (!Placement || !Placement->IsActive() || !Terrain || !GetOwningPlayerPawn()) { Close(); return; }
	DisplayName = FPaths::GetBaseFilename(Placement->GetTemplatePath()).Replace(TEXT("shared_"), TEXT(""));
	if (USWGTreSubsystem* Tre = GetGameInstance()->GetSubsystem<USWGTreSubsystem>())
	{
		FString Table, Key;
		if (Tre->FindTemplateStringId(Placement->GetTemplatePath(), TEXT("objectName"), Table, Key))
		{
			const FString Name = Tre->LookupString(Table, Key);
			if (!Name.IsEmpty()) { DisplayName = Name; }
		}
	}
	Placement->OnPlacementUpdated.AddUniqueDynamic(this, &USWGStructurePlacementWidget::Refresh);
	Placement->OnPlacementEnded.AddUniqueDynamic(this, &USWGStructurePlacementWidget::HandleEnded);
	PlayerOrigin = GetOwningPlayerPawn()->GetActorLocation();
	const FSWGStructureFootprint& Footprint = Placement->GetFootprint();
	BaseHeight = FMath::Clamp(FMath::Max(Footprint.Cols * Footprint.ColChunkSize, Footprint.Rows * Footprint.RowChunkSize) * 500.f, 8000.f, 32000.f);
	Focus = PlayerOrigin + GetOwningPlayerPawn()->GetActorForwardVector() * Footprint.Rows * Footprint.RowChunkSize * 50.f;
	const FVector RawFocus = SWGToRawSpace(Focus);
	Focus.Z = Terrain->GetHeightAt(RawFocus.X, RawFocus.Y) * SWGWorldScale;
	Retarget();
	if (APlayerController* PC = GetOwningPlayer())
	{
		bPreviousCursor = PC->bShowMouseCursor;
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		SetUserFocus(PC);
	}
	Refresh();
}

void USWGStructurePlacementWidget::NativeDestruct()
{
	if (Placement) { Placement->OnPlacementUpdated.RemoveAll(this); Placement->OnPlacementEnded.RemoveAll(this); }
	if (MapMode) { MapMode->OnStateChanged.RemoveAll(this); MapMode->End(); }
	EndHolo();
	View.End(*this);
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->bShowMouseCursor = bPreviousCursor;
		FInputModeGameAndUI Mode;
		PC->SetInputMode(Mode);
	}
	Super::NativeDestruct();
}

void USWGStructurePlacementWidget::Close() { RemoveFromParent(); }
void USWGStructurePlacementWidget::HandleEnded() { Close(); }
void USWGStructurePlacementWidget::Cancel() { if (Placement) { Placement->Cancel(); } }
void USWGStructurePlacementWidget::Rotate() { if (Placement) { Placement->Rotate(1); } }
void USWGStructurePlacementWidget::Place() { if (Placement && Placement->GetValidation().Verdict == ESWGPlacementVerdict::Valid) { Placement->Confirm(); } else { Refresh(); } }
void USWGStructurePlacementWidget::SwitchDroid() { bMovingCameraDroid = !bMovingCameraDroid; Refresh(); SetUserFocus(GetOwningPlayer()); }

void USWGStructurePlacementWidget::ToggleHolo()
{
	if (MapMode && MapMode->IsActive()) { MapMode->End(); }
	if (bHolo) { EndHolo(); Retarget(); Refresh(); return; }
	UWorld* World = GetWorld();
	APawn* Pawn = GetOwningPlayerPawn();
	if (!World || !Pawn || !Terrain) { return; }
	DatapadTarget = NewObject<UTextureRenderTarget2D>(this);
	DatapadTarget->ClearColor = FLinearColor::Black;
	DatapadTarget->InitCustomFormat(960, 540, PF_B8G8R8A8, false);
	DatapadTarget->UpdateResourceImmediate(true);
	if (!DatapadBaseMaterial) { EndHolo(); return; }
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
	if (!DroneCapture || !ProjectorDroid || !CameraDroid) { EndHolo(); return; }
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
				FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &USWGStructurePlacementWidget::PoseDatapadHands, Body));
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
	bHolo = true;
	bMovingCameraDroid = false;
	UpdateHolo();
	Retarget();
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
	Refresh();
	SetUserFocus(GetOwningPlayer());
}

void USWGStructurePlacementWidget::EndHolo()
{
	if (PoseMesh.IsValid()) { PoseMesh->UnregisterOnBoneTransformsFinalizedDelegate(PoseHandle); PoseMesh.Reset(); }
	bHolo = false;
	bDraggingDroid = false;
	HeldHoloKeys.Empty();
	if (DroneCapture) { DroneCapture->Destroy(); DroneCapture = nullptr; }
	if (ProjectorDroid) { ProjectorDroid->Destroy(); ProjectorDroid = nullptr; }
	if (CameraDroid) { CameraDroid->Destroy(); CameraDroid = nullptr; }
	if (DatapadDisplay) { DatapadDisplay->DestroyComponent(); DatapadDisplay = nullptr; }
	if (HeldDatapad) { HeldDatapad->DestroyComponent(); HeldDatapad = nullptr; }
	DatapadTarget = nullptr;
	DatapadMaterial = nullptr;
}

void USWGStructurePlacementWidget::MoveHoloDroid(FVector2D Direction, float DeltaTime, bool bCameraDroid)
{
	const APawn* Pawn = GetOwningPlayerPawn();
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
	UpdateHolo();
	Refresh();
}

void USWGStructurePlacementWidget::UpdateHolo()
{
	if (!bHolo || !Terrain || !Placement || !ProjectorDroid || !CameraDroid || !DroneCapture) { return; }
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

void USWGStructurePlacementWidget::Refresh()
{
	if (!Placement || !StatusText) { return; }
	const FSWGPlacementValidation Validation = Placement->GetValidation();
	const TCHAR* State = Validation.Verdict == ESWGPlacementVerdict::Valid ? TEXT("Ready") : Validation.Verdict == ESWGPlacementVerdict::Uncertain ? TEXT("Server will check") : TEXT("Cannot place");
	StatusText->SetText(FText::FromString(bHolo
		? FString::Printf(TEXT("%s  |  %s  |  %s"), *DisplayName, State, bMovingCameraDroid ? TEXT("Camera droid") : TEXT("Projector droid"))
		: MapMode && MapMode->GetState() == ESWGPlacementMapState::Locked
			? FString::Printf(TEXT("%s  |  Placement locked  |  %s"), *DisplayName, State)
			: MapMode && MapMode->GetState() == ESWGPlacementMapState::FineTune
				? FString::Printf(TEXT("%s  |  Hologram preview  |  %s"), *DisplayName, State)
			: FString::Printf(TEXT("%s  |  %s"), *DisplayName, State)));
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(.75f, .9f, 1.f)));
	if (PromptText)
	{
		const ESWGPlacementMapState MapState = MapMode ? MapMode->GetState() : ESWGPlacementMapState::Off;
		PromptBorder->SetVisibility(MapState == ESWGPlacementMapState::Locked || MapState == ESWGPlacementMapState::FineTune
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		PromptText->SetText(FText::FromString(MapState == ESWGPlacementMapState::Locked
			? TEXT("P: Preview hologram    Enter: Place now    Esc: Unlock")
			: TEXT("Enter: Confirm placement    Esc: Cancel preview")));
	}
}

void USWGStructurePlacementWidget::PoseDatapadHands(USkeletalMeshComponent* Body)
{
	if (!bHolo || !HeldDatapad || !Body || !Body->GetSkeletalMeshAsset()) { return; }
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

void USWGStructurePlacementWidget::Retarget()
{
	if (bHolo && GetOwningPlayerPawn())
	{
		const APawn* Pawn = GetOwningPlayerPawn();
		FVector Eye;
		FRotator EyeRotation;
		Pawn->GetActorEyesViewPoint(Eye, EyeRotation);
		const FVector Forward = Pawn->GetActorForwardVector().GetSafeNormal2D();
		const FVector PadCenter = Eye + Forward * 40.f - FVector(0.f, 0.f, 35.f);
		const FVector Camera = PadCenter - Forward * 85.f + FVector(0.f, 0.f, 200.f);
		if (HeldDatapad) { HeldDatapad->SetWorldLocation(PadCenter); }
		if (!View.GetCamera()) { View.Begin(*this, Camera, PadCenter, 28.f, .35f, 0.f); }
		else { View.Retarget(Camera, PadCenter, 28.f); }
		return;
	}
	const float Height = FMath::Min(BaseHeight * Zoom, MaxPlacementHeight(GetOwningPlayer()));
	const FVector Forward = GetOwningPlayerPawn() ? GetOwningPlayerPawn()->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	const FVector Camera = Focus - Forward * (Height / FMath::Tan(FMath::DegreesToRadians(75.f))) + FVector(0.f, 0.f, Height);
	if (!View.GetCamera()) { View.Begin(*this, Camera, Focus, 70.f, 0.35f, 0.f); }
	else { View.Retarget(Camera, Focus, 70.f); }
}

void USWGStructurePlacementWidget::Pan(FVector2D Direction, float DeltaTime)
{
	const FVector Forward = GetOwningPlayerPawn() ? GetOwningPlayerPawn()->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	FVector Next = Focus + (Forward * Direction.Y + Right * Direction.X) * 2500.f * FMath::Max(1.f, Zoom) * DeltaTime;
	const FVector Delta = (Next - PlayerOrigin).GetClampedToMaxSize2D(10000.f);
	Focus = PlayerOrigin + Delta;
	Focus.Z = SWGToUnrealSpace(FVector(SWGToRawSpace(Focus).X, SWGToRawSpace(Focus).Y, Terrain->GetHeightAt(SWGToRawSpace(Focus).X, SWGToRawSpace(Focus).Y))).Z;
	Retarget();
}

bool USWGStructurePlacementWidget::CursorToTerrain(FVector2D& OutRaw) const
{
	if (!Terrain || !Terrain->GetPlanetData()) { return false; }
	APlayerController* PC = GetOwningPlayer();
	float X, Y;
	FVector Origin, Direction;
	if (!PC || !PC->GetMousePosition(X, Y) || !PC->DeprojectScreenPositionToWorld(X, Y, Origin, Direction)) { return false; }
	// Trace the height field itself so buildings and creatures cannot steal the cursor hit.
	float PreviousT = 0.f;
	float PreviousDelta = SWGToRawSpace(Origin).Z - Terrain->GetHeightAt(SWGToRawSpace(Origin).X, SWGToRawSpace(Origin).Y);
	for (int32 Index = 1; Index <= 48; ++Index)
	{
		const float T = Index * 2500.f;
		const FVector Raw = SWGToRawSpace(Origin + Direction * T);
		const float Delta = Raw.Z - Terrain->GetHeightAt(Raw.X, Raw.Y);
		if (PreviousDelta >= 0.f && Delta <= 0.f)
		{
			float Low = PreviousT, High = T;
			for (int32 Step = 0; Step < 10; ++Step)
			{
				const float Mid = (Low + High) * .5f;
				const FVector At = SWGToRawSpace(Origin + Direction * Mid);
				if (At.Z > Terrain->GetHeightAt(At.X, At.Y)) { Low = Mid; } else { High = Mid; }
			}
			const FVector Hit = SWGToRawSpace(Origin + Direction * ((Low + High) * .5f));
			OutRaw = FVector2D(Hit.X, Hit.Y);
			return true;
		}
		PreviousT = T;
		PreviousDelta = Delta;
	}
	return false;
}

void USWGStructurePlacementWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (!Placement || !Placement->IsActive()) { Close(); return; }
	ViewAge += DeltaTime;
	APlayerController* PC = GetOwningPlayer();
	if (!PC || (ViewAge > .5f && View.GetCamera() && PC->GetViewTarget() != View.GetCamera())) { Placement->Cancel(); return; }
	if (Terrain && Terrain->GetPlanetData() && GetOwningPlayerPawn())
	{
		const FVector PawnLocation = GetOwningPlayerPawn()->GetActorLocation();
		if (FMath::Abs(PawnLocation.Z - PlayerOrigin.Z) > 1000.f)
		{
			Focus += PawnLocation - PlayerOrigin;
			PlayerOrigin = PawnLocation;
			Retarget();
		}
	}
	if (MapMode && MapMode->IsActive()) { MapMode->Tick(DeltaTime); return; }
	if (bHolo)
	{
		// WASD drives the selected droid and the arrows drive the other one, so the camera and the structure
		// can be moved together; both move along the camera droid's forward and right.
		auto IsHeld = [this, PC](const FKey& Key) { return HeldHoloKeys.Contains(Key) || PC->IsInputKeyDown(Key); };
		FVector2D Selected(0.f, 0.f), Other(0.f, 0.f);
		if (IsHeld(EKeys::W)) { Selected.Y += 1.f; }
		if (IsHeld(EKeys::S)) { Selected.Y -= 1.f; }
		if (IsHeld(EKeys::D)) { Selected.X += 1.f; }
		if (IsHeld(EKeys::A)) { Selected.X -= 1.f; }
		if (IsHeld(EKeys::Up)) { Other.Y += 1.f; }
		if (IsHeld(EKeys::Down)) { Other.Y -= 1.f; }
		if (IsHeld(EKeys::Right)) { Other.X += 1.f; }
		if (IsHeld(EKeys::Left)) { Other.X -= 1.f; }
		if (!Selected.IsNearlyZero()) { MoveHoloDroid(Selected.GetSafeNormal(), DeltaTime, bMovingCameraDroid); }
		if (!Other.IsNearlyZero()) { MoveHoloDroid(Other.GetSafeNormal(), DeltaTime, !bMovingCameraDroid); }
		if (IsHeld(EKeys::E)) { CameraHeight = FMath::Clamp(CameraHeight + 900.f * DeltaTime, 500.f, 6000.f); }
		if (IsHeld(EKeys::Q)) { CameraHeight = FMath::Clamp(CameraHeight - 900.f * DeltaTime, 500.f, 6000.f); }
		UpdateHolo();
		Retarget();
		return;
	}
	FVector2D Move(0.f, 0.f);
	if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up)) { Move.Y += 1.f; }
	if (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down)) { Move.Y -= 1.f; }
	if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) { Move.X += 1.f; }
	if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left)) { Move.X -= 1.f; }
	float MouseX, MouseY;
	int32 Width, Height;
	PC->GetViewportSize(Width, Height);
	if (PC->GetMousePosition(MouseX, MouseY))
	{
		const float EdgeX = FMath::Max(24.f, Width * .03f), EdgeY = FMath::Max(24.f, Height * .03f);
		if (MouseX < EdgeX) { Move.X -= 1.f; } else if (MouseX > Width - EdgeX) { Move.X += 1.f; }
		if (MouseY < EdgeY) { Move.Y += 1.f; } else if (MouseY > Height - EdgeY) { Move.Y -= 1.f; }
	}
	if (!Move.IsNearlyZero()) { Pan(Move.GetSafeNormal(), DeltaTime); }
	FVector2D Raw;
	if (CursorToTerrain(Raw) && !Raw.Equals(Placement->GetCandidate(), 0.1f)) { Placement->SetCandidate(Raw); }
}

FReply USWGStructurePlacementWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (MapMode && MapMode->IsActive() && MapMode->HandleKeyDown(Event.GetKey(), Event.IsShiftDown(), Event.IsControlDown())) { return FReply::Handled(); }
	if (MapMode && MapMode->GetState() == ESWGPlacementMapState::FineTune) { return FReply::Unhandled(); }
	if (Event.GetKey() == EKeys::M) { ToggleMap(); return FReply::Handled(); }
	if (Event.GetKey() == EKeys::Escape) { Cancel(); return FReply::Handled(); }
	if (Event.GetKey() == EKeys::Enter || Event.GetKey() == EKeys::SpaceBar) { Place(); return FReply::Handled(); }
	if (Event.GetKey() == EKeys::H) { ToggleHolo(); return FReply::Handled(); }
	if (bHolo && Event.GetKey() == EKeys::Tab) { SwitchDroid(); return FReply::Handled(); }
	if (Event.GetKey() == EKeys::R) { Rotate(); return FReply::Handled(); }
	if (bHolo) { HeldHoloKeys.Add(Event.GetKey()); }
	return FReply::Handled();
}

FReply USWGStructurePlacementWidget::NativeOnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event)
{
	HeldHoloKeys.Remove(Event.GetKey());
	if (MapMode && MapMode->GetState() == ESWGPlacementMapState::FineTune) { return FReply::Unhandled(); }
	return FReply::Handled();
}

void USWGStructurePlacementWidget::NativeOnFocusLost(const FFocusEvent& Event)
{
	HeldHoloKeys.Empty();
	bDraggingDroid = false;
	Super::NativeOnFocusLost(Event);
}

FReply USWGStructurePlacementWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (MapMode && MapMode->IsActive() && MapMode->HandleMouseWheel(Event.GetWheelDelta())) { return FReply::Handled(); }
	if (MapMode && MapMode->GetState() == ESWGPlacementMapState::FineTune) { return FReply::Unhandled(); }
	if (bHolo) { CameraHeight = FMath::Clamp(CameraHeight - Event.GetWheelDelta() * 200.f, 500.f, 6000.f); UpdateHolo(); return FReply::Handled(); }
	if (Event.IsShiftDown()) { Rotate(); }
	else { const float MaxZoom = MaxPlacementHeight(GetOwningPlayer()) / BaseHeight;
		Zoom = FMath::Clamp(Zoom * (Event.GetWheelDelta() > 0.f ? .85f : 1.f / .85f), FMath::Min(.5f, MaxZoom), MaxZoom); Retarget(); }
	return FReply::Handled();
}

FReply USWGStructurePlacementWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (MapMode && MapMode->IsActive() && MapMode->HandleMouseButtonDown(Event.GetEffectingButton()))
	{
		// Right-drag turns the map, so the right button keeps the mouse until released.
		return Event.GetEffectingButton() == EKeys::RightMouseButton ? FReply::Handled().CaptureMouse(TakeWidget()) : FReply::Handled();
	}
	if (MapMode && MapMode->GetState() == ESWGPlacementMapState::FineTune) { return FReply::Unhandled(); }
	if (bHolo)
	{
		if (Event.GetEffectingButton() == EKeys::RightMouseButton) { Rotate(); }
		else if (Event.GetEffectingButton() == EKeys::MiddleMouseButton) { SwitchDroid(); }
		else if (Event.GetEffectingButton() == EKeys::LeftMouseButton) { bDraggingDroid = true; return FReply::Handled().CaptureMouse(TakeWidget()); }
		return FReply::Handled();
	}
	if (Event.GetEffectingButton() == EKeys::RightMouseButton) { Rotate(); }
	else if (Event.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		// Toggle between the default height and the furthest zoom-out.
		const float MaxZoom = MaxPlacementHeight(GetOwningPlayer()) / BaseHeight;
		Zoom = Zoom < MaxZoom * .95f ? MaxZoom : FMath::Min(1.f, MaxZoom);
		Retarget();
	}
	else if (Event.GetEffectingButton() == EKeys::LeftMouseButton) { Place(); }
	return FReply::Handled();
}

FReply USWGStructurePlacementWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (MapMode && MapMode->IsActive() && MapMode->HandleMouseButtonUp(Event.GetEffectingButton())) { return FReply::Handled().ReleaseMouseCapture(); }
	if (MapMode && MapMode->GetState() == ESWGPlacementMapState::FineTune) { return FReply::Unhandled(); }
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && bDraggingDroid)
	{
		bDraggingDroid = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(Geometry, Event);
}

FReply USWGStructurePlacementWidget::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (MapMode && MapMode->IsActive() && MapMode->HandleMouseMove(Event.GetCursorDelta())) { return FReply::Handled(); }
	if (bHolo && bDraggingDroid)
	{
		const FVector2D Delta = Event.GetCursorDelta();
		if (!Delta.IsNearlyZero()) { MoveHoloDroid(FVector2D(Delta.X, -Delta.Y), 1.f / 180.f, bMovingCameraDroid); }
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(Geometry, Event);
}

void USWGStructurePlacementWidget::ToggleMap()
{
	if (MapMode && MapMode->IsActive()) { MapMode->End(); return; }
	if (bHolo) { EndHolo(); }
	if (!MapMode)
	{
		MapMode = NewObject<USWGPlacementMapMode>(this);
		MapMode->OnStateChanged.AddUObject(this, &USWGStructurePlacementWidget::HandleMapStateChanged);
	}
	MapMode->Begin(this, &View);
}

void USWGStructurePlacementWidget::FineTune()
{
	if (MapMode && MapMode->CanFineTune()) { MapMode->BeginFineTune(); }
}

void USWGStructurePlacementWidget::HandleMapStateChanged()
{
	if (!MapMode) { return; }
	if (!MapMode->IsActive()) { Retarget(); }
	Refresh();
	if (APlayerController* PC = GetOwningPlayer()) { SetUserFocus(PC); }
}
