#pragma once

#include "CoreMinimal.h"
#include "SWGPlacementViewMode.h"
#include "SWGPlacementMapMode.generated.h"

class ASWGHoloMapActor;
class ASWGStructureHoloDroid;
class USWGStructurePlacementSubsystem;
class USWGTerrainSubsystem;

UENUM()
enum class ESWGPlacementMapState : uint8
{
	Off,
	/** The cursor moves the structure across the holo map. */
	Browsing,
	/** A position is chosen: place it, or preview it in the world. */
	Locked,
	/** The structure is projected by a droid at the locked site, seen from the character. */
	FineTune,
};

/**
 * The holo map placement view. The holo map (real buildings under a faint hologram) becomes a table model
 * of the placement: the structure follows the cursor over it, a click locks the spot, and from there it is placed
 * directly or fine tuned as a hologram in the world. The placement widget owns the camera (FSWGCameraTakeover) and
 * delegates input and ticking here while the mode is active.
 */
UCLASS()
class SWGUIHOLO_API USWGPlacementMapMode : public USWGPlacementViewMode
{
	GENERATED_BODY()

public:
	virtual bool Begin(UUserWidget* InOwner, FSWGCameraTakeover* InView) override;
	virtual void End() override;
	virtual bool IsActive() const override { return State != ESWGPlacementMapState::Off; }
	virtual void Tick(float DeltaTime) override;
	virtual bool HandleKeyDown(const FKey& Key, bool bShift, bool bControl) override;
	virtual bool HandleMouseButtonDown(const FKey& Button) override;
	/** Releasing the right button after turning the map ends the turn; without a drag it rotates the structure. */
	virtual bool HandleMouseButtonUp(const FKey& Button) override;
	/** Right-drag turns or tilts the map; true when consumed. */
	virtual bool HandleMouseMove(const FVector2D& CursorDelta) override;
	virtual bool HandleMouseWheel(float Delta) override;
	virtual bool HandleCommand(FName Command) override;
	virtual bool PassesInputToWorld() const override { return State == ESWGPlacementMapState::FineTune; }
	virtual bool WantsMouseCapture(const FKey& Button) const override { return Button == EKeys::RightMouseButton; }
	virtual FString GetStatusDetail() const override;
	virtual FString GetPrompt() const override;

	ESWGPlacementMapState GetState() const { return State; }
	bool CanFineTune() const;
	void BeginFineTune();
	void SelectCandidate(const FVector2D& Raw);
	FText GetHint() const;

	/** Metres from the map's centre to its rim; the server's placement range is 100 m. */
	static constexpr float StartRadius = 100.f;
	static constexpr float MinRadius = 40.f;
	static constexpr float MaxRadius = 100.f;

private:
	void SetState(ESWGPlacementMapState NewState);
	void UpdateMap(float DeltaTime);
	void UpdateMapCamera();
	void UpdatePreviewDroid();
	bool EnsurePreviewDroid();
	bool CursorToRaw(FVector2D& OutRaw) const;
	void ConfirmPlacement();
	/** The player's position in raw metres. */
	bool GetPlayerRaw(FVector2D& OutPosition) const;

	UPROPERTY() TObjectPtr<UUserWidget> Owner;
	UPROPERTY() TObjectPtr<USWGStructurePlacementSubsystem> Placement;
	UPROPERTY() TObjectPtr<USWGTerrainSubsystem> Terrain;
	UPROPERTY() TObjectPtr<ASWGHoloMapActor> Hologram;
	UPROPERTY() TObjectPtr<ASWGStructureHoloDroid> PreviewDroid;
	FSWGCameraTakeover* View = nullptr;
	ESWGPlacementMapState State = ESWGPlacementMapState::Off;
	FVector2D MapCenter = FVector2D::ZeroVector;
	FVector2D LastPreviewPlayer = FVector2D::ZeroVector;
	float MapRadius = StartRadius;
	bool bTurning = false;
	float TurnDragPixels = 0.f;
	FVector MapCameraLocation = FVector::ZeroVector;
	FVector2D MapCameraDirection = FVector2D(1.f, 0.f);
	float MapTiltDegrees = 0.f;
};
