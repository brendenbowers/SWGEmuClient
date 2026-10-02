#include "SWGPlacementMapMode.h"
#include "SWGHoloProjector.h"
#include "SWGHoloMapActor.h"
#include "SWGHoloMapPlacementLayer.h"
#include "SWGHoloMapRealisticAppearance.h"
#include "SWGStructureHoloDroid.h"
#include "SWGCameraTakeover.h"
#include "Subsystems/SWGStructurePlacementSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Objects/World/SWGStructurePlacementPreview.h"
#include "Structure/SWGPlacementRules.h"
#include "Common/SWGWorldScale.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

namespace
{
	// The holo map widget's projector stand and over-the-shoulder camera, so both read as the same device.
	constexpr float ProjectorDistance = 170.f;
	constexpr float ProjectorHeight = 95.f;
	const FVector CameraOffset(-250.f, 100.f, 168.f);
	constexpr float PanRadiiPerSecond = 0.6f;
	constexpr float WheelZoomFactor = 0.85f;
	constexpr float PlacementRangeMetres = 100.f;
	constexpr float MapFieldOfView = 70.f;

	/** UE ground direction (x north, y east) to raw (x east, y north). */
	FVector2D UEToRaw(const FVector& Direction)
	{
		return FVector2D(Direction.Y, Direction.X);
	}

	const FLinearColor PlacementHoloColor(.12f, .55f, 1.f);
}

bool USWGPlacementMapMode::Begin(UUserWidget* InOwner, FSWGCameraTakeover* InView)
{
	if (!InOwner || !InView || IsActive() || !InView->GetCamera()) { return false; }
	UGameInstance* GameInstance = InOwner->GetGameInstance();
	APawn* Pawn = InOwner->GetOwningPlayerPawn();
	UWorld* World = InOwner->GetWorld();
	Placement = GameInstance ? GameInstance->GetSubsystem<USWGStructurePlacementSubsystem>() : nullptr;
	Terrain = GameInstance ? GameInstance->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
	if (!Placement || !Placement->IsActive() || !Terrain || !Pawn || !World || !Terrain->GetPlanetData()) { return false; }

	const FRotator Facing(0.f, Pawn->GetActorRotation().Yaw, 0.f);
	FVector ProjectorLocation;
	SWGHoloProjector::FindLocation(Pawn, ProjectorDistance, ProjectorHeight, ProjectorLocation);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	// World-aligned, like the map widget's: north on the hologram is north outside.
	Hologram = World->SpawnActor<ASWGHoloMapActor>(ASWGHoloMapActor::StaticClass(), ProjectorLocation, FRotator::ZeroRotator, Params);
	if (!Hologram) { return false; }
	Hologram->SetAppearance(USWGHoloMapRealisticAppearance::StaticClass());
	Hologram->AddLayer<USWGHoloMapPlacementLayer>();

	Owner = InOwner;
	View = InView;
	Hologram->SetMinViewRadius(MinRadius);
	MapRadius = StartRadius;
	Hologram->SetViewRadius(MapRadius);
	GetPlayerRaw(MapCenter);
	Hologram->SetViewCenter(MapCenter);
	Hologram->SetDroidSide(FVector2D(Facing.Vector()));
	MapCameraLocation = ProjectorLocation + Facing.RotateVector(CameraOffset);
	const FVector CameraFromFocus = MapCameraLocation - Hologram->GetFocusLocation();
	MapCameraDirection = FVector2D(CameraFromFocus.X, CameraFromFocus.Y).GetSafeNormal();
	MapTiltDegrees = 0.f;
	Hologram->SetViewTilt(MapTiltDegrees, MapCameraDirection);
	Placement->SetPreviewHidden(true);
	UpdateMapCamera();
	SetState(ESWGPlacementMapState::Browsing);
	return true;
}

void USWGPlacementMapMode::End()
{
	if (!IsActive()) { return; }
	if (PreviewDroid) { PreviewDroid->Destroy(); PreviewDroid = nullptr; }
	if (Hologram)
	{
		Hologram->Destroy();
		Hologram = nullptr;
	}
	if (Placement)
	{
		if (ASWGStructurePlacementPreview* Preview = Placement->GetPreview()) { Preview->SetHologramOnly(false); }
		Placement->SetPreviewHidden(false);
	}
	View = nullptr;
	Owner = nullptr;
	bTurning = false;
	SetState(ESWGPlacementMapState::Off);
}

void USWGPlacementMapMode::SetState(ESWGPlacementMapState NewState)
{
	if (State == NewState) { return; }
	State = NewState;
	OnStateChanged.Broadcast();
}

bool USWGPlacementMapMode::GetPlayerRaw(FVector2D& OutPosition) const
{
	const APawn* Pawn = Owner ? Owner->GetOwningPlayerPawn() : nullptr;
	if (!Pawn) { return false; }
	const FVector Raw = SWGToRawSpace(Pawn->GetActorLocation());
	OutPosition = FVector2D(Raw.X, Raw.Y);
	return true;
}

bool USWGPlacementMapMode::CanFineTune() const
{
	return State == ESWGPlacementMapState::Locked && Placement && Placement->GetValidation().Verdict == ESWGPlacementVerdict::Valid;
}

void USWGPlacementMapMode::BeginFineTune()
{
	if (!CanFineTune() || !Owner || !Owner->GetWorld()) { return; }
	const FVector2D Candidate = Placement->GetCandidate();
	const FVector Ground = SWGToUnrealSpace(FVector(Candidate.X, Candidate.Y, Terrain->GetHeightAt(Candidate.X, Candidate.Y)));
	if (!EnsurePreviewDroid()) { return; }
	if (Hologram) { Hologram->SetActorHiddenInGame(true); }
	if (ASWGStructurePlacementPreview* Preview = Placement->GetPreview()) { Preview->SetHologramOnly(true); }
	Placement->SetPreviewHidden(false);
	if (View) { View->End(*Owner); }
	GetPlayerRaw(LastPreviewPlayer);
	SetState(ESWGPlacementMapState::FineTune);
	UpdatePreviewDroid();
	if (APlayerController* PC = Owner->GetOwningPlayer())
	{
		FVector Eye;
		FRotator EyeRotation;
		Owner->GetOwningPlayerPawn()->GetActorEyesViewPoint(Eye, EyeRotation);
		const FVector Aim = FMath::Lerp(Ground, PreviewDroid->GetDroidLocation(), .55f);
		const FRotator TowardSite = (Aim - Eye).Rotation();
		PC->SetControlRotation(FRotator(FMath::Clamp(TowardSite.Pitch, -15.f, 20.f), TowardSite.Yaw, 0.f));
	}
}

void USWGPlacementMapMode::Tick(float DeltaTime)
{
	if (!IsActive() || !Placement || !Placement->IsActive() || !Owner) { return; }
	if (State == ESWGPlacementMapState::FineTune)
	{
		FVector2D PlayerRaw;
		if (GetPlayerRaw(PlayerRaw) && !PlayerRaw.Equals(LastPreviewPlayer, 1.f))
		{
			LastPreviewPlayer = PlayerRaw;
			Placement->SetCandidate(Placement->GetCandidate());
		}
		UpdatePreviewDroid();
	}
	else { UpdateMap(DeltaTime); }
}

bool USWGPlacementMapMode::CursorToRaw(FVector2D& OutRaw) const
{
	const APlayerController* PC = Owner ? Owner->GetOwningPlayer() : nullptr;
	float X = 0.f, Y = 0.f;
	FVector Origin, Direction;
	return Hologram && PC && PC->GetMousePosition(X, Y) && PC->DeprojectScreenPositionToWorld(X, Y, Origin, Direction)
		&& Hologram->RayToRaw(Origin, Direction, OutRaw);
}

void USWGPlacementMapMode::UpdateMap(float DeltaTime)
{
	APlayerController* PC = Owner->GetOwningPlayer();
	const APawn* Pawn = Owner->GetOwningPlayerPawn();
	if (!PC || !Pawn || !Hologram) { return; }
	UpdateMapCamera();

	FVector2D Move(0.f, 0.f);
	if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up)) { Move.Y += 1.f; }
	if (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down)) { Move.Y -= 1.f; }
	if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) { Move.X += 1.f; }
	if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left)) { Move.X -= 1.f; }
	if (!Move.IsNearlyZero())
	{
		const FVector Forward = Pawn->GetActorForwardVector().GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector2D RawMove = UEToRaw(FRotator(0.f, -Hologram->GetViewYaw(), 0.f).RotateVector(Forward * Move.Y + Right * Move.X)).GetSafeNormal();
		MapCenter += RawMove * MapRadius * PanRadiiPerSecond * DeltaTime;
		// Keep the entire disc inside the server's placement range.
		FVector2D PlayerRaw;
		if (GetPlayerRaw(PlayerRaw))
		{
			MapCenter = PlayerRaw + (MapCenter - PlayerRaw).GetClampedToMaxSize(PlacementRangeMetres - MapRadius);
		}
		Hologram->SetViewCenter(MapCenter);
	}

	FVector2D Raw;
	if (State == ESWGPlacementMapState::Browsing && !bTurning && CursorToRaw(Raw) && !Raw.Equals(Placement->GetCandidate(), 0.05f))
	{
		Placement->SetCandidate(Raw);
	}

	// The world ghost's mesh and turn, shown on the map in the verdict colour.
	const ASWGStructurePlacementPreview* Preview = Placement->GetPreview();
	const UStaticMeshComponent* Ghost = Preview ? Preview->GetGhostComponent() : nullptr;
	USWGHoloMapPlacementLayer* PlacementLayer = Hologram->FindLayer<USWGHoloMapPlacementLayer>();
	if (!PlacementLayer) { return; }
	PlacementLayer->SetGhost(Ghost ? Ghost->GetStaticMesh() : nullptr, Placement->GetCandidate(),
		Ghost ? Ghost->GetComponentQuat() : FQuat::Identity, PlacementHoloColor);

	// What the server reserves around every nearby structure; the ones stopping this placement are brighter.
	TArray<USWGHoloMapPlacementLayer::FNoBuildArea> Areas;
	for (const FSWGPlacementNeighbour& Neighbour : Placement->GetNeighbours())
	{
		if (Neighbour.bHasRect)
		{
			USWGHoloMapPlacementLayer::FNoBuildArea& Area = Areas.AddDefaulted_GetRef();
			Area.Rect = Neighbour.Rect;
			Area.Center = Neighbour.Rect.GetCenter();
			Area.bBlocking = Neighbour.bBlocking;
		}
		if (Neighbour.NoBuildRadius > 0.f)
		{
			USWGHoloMapPlacementLayer::FNoBuildArea& Area = Areas.AddDefaulted_GetRef();
			Area.bCircle = true;
			Area.Center = Neighbour.Center;
			Area.Radius = Neighbour.NoBuildRadius;
		}
	}
	// And the zone this structure would reserve itself.
	const FSWGStructureFootprint& Footprint = Placement->GetFootprint();
	if (Footprint.IsValid())
	{
		const FBox2D Local = FSWGPlacementRules::GetServerFootprintRect(Footprint, Placement->GetRotation() * 90);
		USWGHoloMapPlacementLayer::FNoBuildArea& Area = Areas.AddDefaulted_GetRef();
		Area.Rect = FBox2D(Local.Min + Placement->GetCandidate(), Local.Max + Placement->GetCandidate());
		Area.Center = Area.Rect.GetCenter();
		Area.bPlacing = true;
		Area.Tint = PlacementHoloColor;
	}
	PlacementLayer->SetNoBuildAreas(Areas);
}

void USWGPlacementMapMode::UpdateMapCamera()
{
	if (!Hologram || !View) { return; }
	const FVector Focus = Hologram->GetFocusLocation();
	View->Retarget(MapCameraLocation, Focus, MapFieldOfView);
}

void USWGPlacementMapMode::UpdatePreviewDroid()
{
	const APawn* Pawn = Owner ? Owner->GetOwningPlayerPawn() : nullptr;
	if (!Pawn || !Terrain) { return; }
	const FVector2D Candidate = Placement->GetCandidate();
	const FVector Ground = SWGToUnrealSpace(FVector(Candidate.X, Candidate.Y, Terrain->GetHeightAt(Candidate.X, Candidate.Y)));
	const FVector Forward = Pawn->GetActorForwardVector().GetSafeNormal2D();
	if (PreviewDroid)
	{
		PreviewDroid->SetActorLocation(Ground);
		const ASWGStructurePlacementPreview* Preview = Placement->GetPreview();
		const UStaticMeshComponent* Ghost = Preview ? Preview->GetGhostComponent() : nullptr;
		const float RoofHeight = Ghost && Ghost->GetStaticMesh()
			? Ghost->Bounds.Origin.Z + Ghost->Bounds.BoxExtent.Z - Ground.Z : 0.f;
		PreviewDroid->DroidHeight = FMath::Max(900.f, RoofHeight + 500.f) / (PreviewDroid->DiscDiameter * .5f);
		PreviewDroid->SetDroidSide(FVector2D(Forward.X, Forward.Y));
		const FSWGStructureFootprint& Footprint = Placement->GetFootprint();
		const FBox2D Local = Footprint.IsValid() ? FSWGPlacementRules::GetServerFootprintRect(Footprint, Placement->GetRotation() * 90)
			: FBox2D(FVector2D(-4.f, -4.f), FVector2D(4.f, 4.f));
		const FVector2D Corners[] = { Local.Min, FVector2D(Local.Max.X, Local.Min.Y), Local.Max, FVector2D(Local.Min.X, Local.Max.Y) };
		TArray<FVector> Targets;
		TArray<float> Brightness;
		for (const FVector2D& Corner : Corners)
		{
			const FVector2D Raw = Candidate + Corner;
			Targets.Add(SWGToUnrealSpace(FVector(Raw.X, Raw.Y, Terrain->GetHeightAt(Raw.X, Raw.Y) + .2f)));
			Brightness.Add(1.f);
		}
		Targets.Add(Ground + FVector(0.f, 0.f, 900.f));
		Brightness.Add(1.2f);
		PreviewDroid->SetExtraRays(Targets, Brightness);
	}
}

bool USWGPlacementMapMode::EnsurePreviewDroid()
{
	if (PreviewDroid) { return true; }
	if (!Owner || !Owner->GetWorld() || !Placement || !Terrain) { return false; }
	const FVector2D Candidate = Placement->GetCandidate();
	const FVector Ground = SWGToUnrealSpace(FVector(Candidate.X, Candidate.Y, Terrain->GetHeightAt(Candidate.X, Candidate.Y)));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	PreviewDroid = Owner->GetWorld()->SpawnActor<ASWGStructureHoloDroid>(ASWGStructureHoloDroid::StaticClass(), Ground, FRotator::ZeroRotator, Params);
	if (!PreviewDroid) { return false; }
	PreviewDroid->DroidReach = 0.f;
	PreviewDroid->DroidScale = 10.f;
	PreviewDroid->SetProjectionSurfaceVisible(false);
	UpdatePreviewDroid();
	return true;
}

void USWGPlacementMapMode::ConfirmPlacement()
{
	if (!Placement || Placement->GetValidation().Verdict != ESWGPlacementVerdict::Valid) { return; }
	EnsurePreviewDroid();
	ASWGStructureHoloDroid* Projector = PreviewDroid;
	PreviewDroid = nullptr;
	if (!Placement->ConfirmWithProjector(Projector)) { PreviewDroid = Projector; }
}

void USWGPlacementMapMode::SelectCandidate(const FVector2D& Raw)
{
	if (!IsActive() || !Placement || State == ESWGPlacementMapState::FineTune) { return; }
	Placement->SetCandidate(Raw);
	if (Placement->GetValidation().Verdict == ESWGPlacementVerdict::Valid) { SetState(ESWGPlacementMapState::Locked); }
	else { SetState(ESWGPlacementMapState::Browsing); }
}

bool USWGPlacementMapMode::HandleKeyDown(const FKey& Key, bool bShift, bool bControl)
{
	if (!IsActive()) { return false; }
	if (Key == EKeys::Escape)
	{
		if (State == ESWGPlacementMapState::FineTune)
		{
			// Back to the map with the position still locked.
			if (PreviewDroid) { PreviewDroid->Destroy(); PreviewDroid = nullptr; }
			if (Hologram) { Hologram->SetActorHiddenInGame(false); }
			if (ASWGStructurePlacementPreview* Preview = Placement->GetPreview()) { Preview->SetHologramOnly(false); }
			Placement->SetPreviewHidden(true);
			if (View && Owner && Hologram) { View->Begin(*Owner, MapCameraLocation, Hologram->GetFocusLocation(), MapFieldOfView, .35f, 0.f); }
			SetState(ESWGPlacementMapState::Locked);
		}
		else if (State == ESWGPlacementMapState::Locked) { SetState(ESWGPlacementMapState::Browsing); }
		else { End(); }
		return true;
	}
	if ((Key == EKeys::P || Key == EKeys::F) && State == ESWGPlacementMapState::Locked) { BeginFineTune(); return true; }
	if ((Key == EKeys::Enter || Key == EKeys::SpaceBar) && State != ESWGPlacementMapState::Browsing) { ConfirmPlacement(); return true; }
	if (State == ESWGPlacementMapState::FineTune)
	{
		return Key != EKeys::W && Key != EKeys::A && Key != EKeys::S && Key != EKeys::D
			&& Key != EKeys::Up && Key != EKeys::Down && Key != EKeys::Left && Key != EKeys::Right
			&& Key != EKeys::LeftShift && Key != EKeys::RightShift;
	}
	return false;
}

bool USWGPlacementMapMode::HandleMouseButtonDown(const FKey& Button)
{
	if (!IsActive() || !Placement) { return false; }
	if (State == ESWGPlacementMapState::FineTune) { return Button != EKeys::RightMouseButton; }
	if (Button == EKeys::RightMouseButton)
	{
		// Held and dragged it turns the map; a plain click (see HandleMouseButtonUp) rotates the structure.
		bTurning = true;
		TurnDragPixels = 0.f;
		return true;
	}
	if (Button == EKeys::LeftMouseButton && State != ESWGPlacementMapState::FineTune)
	{
		FVector2D Raw;
		// A click picks (or moves) the spot and locks it; off the map it just keeps the current one.
		if (CursorToRaw(Raw)) { SelectCandidate(Raw); }
		else if (Placement->GetValidation().Verdict == ESWGPlacementVerdict::Valid) { SetState(ESWGPlacementMapState::Locked); }
	}
	return true;
}

bool USWGPlacementMapMode::HandleMouseWheel(float Delta)
{
	if (!IsActive()) { return false; }
	if (State == ESWGPlacementMapState::FineTune) { return false; }
	if (Hologram)
	{
		MapRadius = FMath::Clamp(MapRadius * (Delta > 0.f ? WheelZoomFactor : 1.f / WheelZoomFactor), MinRadius, MaxRadius);
		Hologram->SetViewRadius(MapRadius);
		FVector2D PlayerRaw;
		if (GetPlayerRaw(PlayerRaw))
		{
			MapCenter = PlayerRaw + (MapCenter - PlayerRaw).GetClampedToMaxSize(PlacementRangeMetres - MapRadius);
			Hologram->SetViewCenter(MapCenter);
		}
	}
	return true;
}

FText USWGPlacementMapMode::GetHint() const
{
	switch (State)
	{
	case ESWGPlacementMapState::Browsing:
		return FText::FromString(TEXT("Move over the map to position the structure   WASD: pan   Wheel: zoom   R or tap right click: rotate\nRight-drag left/right: turn   Right-drag up/down: tilt   Click: lock the position   M or Esc: leave the map"));
	case ESWGPlacementMapState::Locked:
		return FText::FromString(TEXT("Placement locked   P: hologram preview   Enter: place immediately   Esc: unlock"));
	case ESWGPlacementMapState::FineTune:
		return FText::FromString(TEXT("Hologram preview from the character's view   Enter: confirm placement   Esc: back to the map"));
	default:
		return FText();
	}
}

bool USWGPlacementMapMode::HandleMouseButtonUp(const FKey& Button)
{
	if (!IsActive() || Button != EKeys::RightMouseButton || !bTurning) { return false; }
	bTurning = false;
	// Barely moved: it was a tap, which rotates the structure.
	if (TurnDragPixels < 4.f && Placement) { Placement->Rotate(1); }
	return true;
}

bool USWGPlacementMapMode::HandleMouseMove(const FVector2D& CursorDelta)
{
	if (!IsActive() || !bTurning || !Hologram) { return false; }
	TurnDragPixels += FMath::Abs(CursorDelta.X) + FMath::Abs(CursorDelta.Y);
	if (TurnDragPixels >= 4.f)
	{
		Hologram->SetViewYaw(Hologram->GetViewYaw() + CursorDelta.X * 0.3f);
		MapTiltDegrees = FMath::Clamp(MapTiltDegrees - CursorDelta.Y * 0.3f, 0.f, 60.f);
		Hologram->SetViewTilt(MapTiltDegrees, MapCameraDirection);
	}
	return true;
}

bool USWGPlacementMapMode::HandleCommand(FName Command)
{
	if (Command == TEXT("FineTune") && CanFineTune()) { BeginFineTune(); return true; }
	return false;
}

FString USWGPlacementMapMode::GetStatusDetail() const
{
	return State == ESWGPlacementMapState::Locked ? TEXT("Placement locked")
		: State == ESWGPlacementMapState::FineTune ? TEXT("Hologram preview") : FString();
}

FString USWGPlacementMapMode::GetPrompt() const
{
	return State == ESWGPlacementMapState::Locked ? TEXT("P: Preview hologram    Enter: Place now    Esc: Unlock")
		: State == ESWGPlacementMapState::FineTune ? TEXT("Enter: Confirm placement    Esc: Cancel preview") : FString();
}
