#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "SWGHoloView.h"
#include "SWGStructurePlacementWidget.generated.h"

class USWGStructurePlacementSubsystem;
class USWGTerrainSubsystem;
class UTextBlock;
class UBorder;
class UVerticalBox;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class ASceneCapture2D;
class ASWGStructureHoloDroid;
class USWGPlacementMapMode;

/** Full-screen controls for a structure placement session. */
UCLASS()
class SWGUI_API USWGStructurePlacementWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	USWGStructurePlacementWidget(const FObjectInitializer& ObjectInitializer);
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply NativeOnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual void NativeOnFocusLost(const FFocusEvent& Event) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;

	void Close();
	UFUNCTION()
	void ToggleHolo();
	/** The third view: the structure on a realistic holo map, then placed directly or fine tuned in the world. */
	UFUNCTION()
	void ToggleMap();
	UFUNCTION()
	void FineTune();

private:
	UFUNCTION() void Refresh();
	UFUNCTION() void HandleEnded();
	UFUNCTION() void Place();
	UFUNCTION() void Rotate();
	UFUNCTION() void Cancel();
	UFUNCTION() void SwitchDroid();
	void Retarget();
	void Pan(FVector2D Direction, float DeltaTime);
	void MoveHoloDroid(FVector2D Direction, float DeltaTime, bool bCameraDroid);
	void UpdateHolo();
	void EndHolo();
	void HandleMapStateChanged();
	void PoseDatapadHands(USkeletalMeshComponent* Body);
	bool CursorToTerrain(FVector2D& OutRaw) const;

	UPROPERTY() TObjectPtr<USWGStructurePlacementSubsystem> Placement;
	UPROPERTY() TObjectPtr<USWGTerrainSubsystem> Terrain;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<UTextBlock> PromptText;
	UPROPERTY() TObjectPtr<UBorder> PromptBorder;
	UPROPERTY() TObjectPtr<UVerticalBox> PlacementPanel;
	UPROPERTY() TObjectPtr<USWGPlacementMapMode> MapMode;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> DatapadTarget;
	UPROPERTY() TObjectPtr<UMaterialInterface> DatapadBaseMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DatapadMaterial;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> HeldDatapad;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> DatapadDisplay;
	TWeakObjectPtr<USkeletalMeshComponent> PoseMesh;
	FDelegateHandle PoseHandle;
	UPROPERTY() TObjectPtr<ASceneCapture2D> DroneCapture;
	UPROPERTY() TObjectPtr<ASWGStructureHoloDroid> ProjectorDroid;
	UPROPERTY() TObjectPtr<ASWGStructureHoloDroid> CameraDroid;
	FSWGHoloView View;
	FVector Focus = FVector::ZeroVector;
	FVector PlayerOrigin = FVector::ZeroVector;
	FString DisplayName;
	float BaseHeight = 4000.f;
	float Zoom = 1.f;
	float ViewAge = 0.f;
	bool bPreviousCursor = false;
	bool bHolo = false;
	bool bMovingCameraDroid = false;
	bool bDraggingDroid = false;
	TSet<FKey> HeldHoloKeys;
	FVector2D CameraRaw = FVector2D::ZeroVector;
	float CameraHeight = 3500.f;
};
