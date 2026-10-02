#pragma once

#include "CoreMinimal.h"
#include "SWGPlacementViewMode.h"
#include "SWGPlacementDatapadMode.generated.h"

class ASWGStructureHoloDroid;
class ASceneCapture2D;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class USWGStructurePlacementSubsystem;
class USWGTerrainSubsystem;

/**
 * The holo datapad placement view: the character holds a datapad whose screen shows what a camera droid sees, and a
 * projector droid beams the structure at the candidate site. WASD drives the selected droid and the arrow keys the
 * other (Tab or middle click switches), Q/E and the wheel set the camera droid's height, and dragging with the left
 * button moves the selected droid.
 */
UCLASS()
class SWGUIHOLO_API USWGPlacementDatapadMode : public USWGPlacementViewMode
{
	GENERATED_BODY()

public:
	USWGPlacementDatapadMode();

	virtual bool Begin(UUserWidget* InOwner, FSWGCameraTakeover* InView) override;
	virtual void End() override;
	virtual bool IsActive() const override { return bActive; }
	virtual void Tick(float DeltaTime) override;
	virtual bool HandleKeyDown(const FKey& Key, bool bShift, bool bControl) override;
	virtual bool HandleKeyUp(const FKey& Key) override;
	virtual bool HandleMouseButtonDown(const FKey& Button) override;
	virtual bool HandleMouseButtonUp(const FKey& Button) override;
	virtual bool HandleMouseMove(const FVector2D& CursorDelta) override;
	virtual bool HandleMouseWheel(float Delta) override;
	virtual void HandleFocusLost() override;
	virtual bool WantsMouseCapture(const FKey& Button) const override { return Button == EKeys::LeftMouseButton; }
	virtual FString GetStatusDetail() const override { return bMovingCameraDroid ? TEXT("Camera droid") : TEXT("Projector droid"); }

private:
	void SwitchDroid();
	void MoveDroid(FVector2D Direction, float DeltaTime, bool bCameraDroid);
	/** Places both droids, the capture and the projector's rays for the current candidate. */
	void UpdateScene();
	/** Frames the held datapad from over the character's shoulder. */
	void UpdateCamera();
	void PoseHands(USkeletalMeshComponent* Body);

	UPROPERTY() TObjectPtr<UUserWidget> Owner;
	UPROPERTY() TObjectPtr<USWGStructurePlacementSubsystem> Placement;
	UPROPERTY() TObjectPtr<USWGTerrainSubsystem> Terrain;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> DatapadTarget;
	UPROPERTY() TObjectPtr<UMaterialInterface> DatapadBaseMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DatapadMaterial;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> HeldDatapad;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> DatapadDisplay;
	UPROPERTY() TObjectPtr<ASceneCapture2D> DroneCapture;
	UPROPERTY() TObjectPtr<ASWGStructureHoloDroid> ProjectorDroid;
	UPROPERTY() TObjectPtr<ASWGStructureHoloDroid> CameraDroid;
	TWeakObjectPtr<USkeletalMeshComponent> PoseMesh;
	FDelegateHandle PoseHandle;
	FSWGCameraTakeover* View = nullptr;
	FVector PlayerOrigin = FVector::ZeroVector;
	FVector2D CameraRaw = FVector2D::ZeroVector;
	float CameraHeight = 3500.f;
	TSet<FKey> HeldKeys;
	bool bActive = false;
	bool bMovingCameraDroid = false;
	bool bDragging = false;
};
