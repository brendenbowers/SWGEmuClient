#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "InputCoreTypes.h"
#include "SWGPlacementMapMode.generated.h"

class ASWGHoloMapActor;
class ASWGStructureHoloDroid;
class UUserWidget;
class USWGStructurePlacementSubsystem;
class USWGTerrainSubsystem;
class FSWGHoloView;

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
 * The placement widget's third view. The holo map (real buildings under a faint hologram) becomes a table model
 * of the placement: the structure follows the cursor over it, a click locks the spot, and from there it is placed
 * directly or fine tuned as a hologram in the world. The widget owns the camera (FSWGHoloView) and delegates
 * input and ticking here while the mode is active.
 */
UCLASS()
class SWGUI_API USWGPlacementMapMode : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnStateChanged);
	FOnStateChanged OnStateChanged;

	bool Begin(UUserWidget* InOwner, FSWGHoloView* InView);
	void End();
	bool IsActive() const { return State != ESWGPlacementMapState::Off; }
	ESWGPlacementMapState GetState() const { return State; }
	bool CanFineTune() const;
	void BeginFineTune();
	void SelectCandidate(const FVector2D& Raw);

	void Tick(float DeltaTime);
	/** True when the mode consumed the key. */
	bool HandleKeyDown(const FKey& Key, bool bShift, bool bControl);
	bool HandleMouseButtonDown(const FKey& Button);
	/** Releasing the right button after turning the map ends the turn; without a drag it rotates the structure. */
	bool HandleMouseButtonUp(const FKey& Button);
	/** Right-drag turns or tilts the map; true when consumed. */
	bool HandleMouseMove(const FVector2D& CursorDelta);
	bool HandleMouseWheel(float Delta);
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
	FSWGHoloView* View = nullptr;
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
