#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "SWGCameraTakeover.h"
#include "SWGStructurePlacementWidget.generated.h"

class USWGStructurePlacementSubsystem;
class USWGTerrainSubsystem;
class USWGPlacementViewMode;
class UTextBlock;
class UBorder;
class UVerticalBox;

/**
 * Full-screen controls for a structure placement session, with the overhead camera as its own view. Further views
 * (the holo datapad, the holo map) are USWGPlacementViewMode classes that SWGUIHolo registers; the widget builds a
 * key for each from FSWGPlacementViewRegistry, offers the active one every key and mouse event first, and handles
 * what it leaves (rotate, place, cancel). With no visual plugin beyond this one, only the overhead view exists.
 */
UCLASS()
class SWGUIWINDOW_API USWGStructurePlacementWidget : public UUserWidget
{
	GENERATED_BODY()
public:
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

	/** Switches to the registered view with this id, or back to the overhead view if it is already the active one. */
	void ToggleMode(FName ModeId);

private:
	UFUNCTION() void Refresh();
	UFUNCTION() void HandleEnded();
	UFUNCTION() void Place();
	UFUNCTION() void Rotate();
	UFUNCTION() void Cancel();
	void Retarget();
	void Pan(FVector2D Direction, float DeltaTime);
	void HandleModeStateChanged();
	void HandleModeCommand(FName ModeId, FName Command);
	bool CursorToTerrain(FVector2D& OutRaw) const;
	bool IsModeActive() const;
	bool ActiveModePassesInputToWorld() const;

	UPROPERTY() TObjectPtr<USWGStructurePlacementSubsystem> Placement;
	UPROPERTY() TObjectPtr<USWGTerrainSubsystem> Terrain;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<UTextBlock> PromptText;
	UPROPERTY() TObjectPtr<UBorder> PromptBorder;
	UPROPERTY() TObjectPtr<UVerticalBox> PlacementPanel;
	/** One instance per registered view, created the first time it is toggled on. */
	UPROPERTY() TMap<FName, TObjectPtr<USWGPlacementViewMode>> Modes;
	UPROPERTY() TObjectPtr<USWGPlacementViewMode> ActiveMode;
	FDelegateHandle ToggleRequestedHandle;
	FDelegateHandle CommandRequestedHandle;
	FSWGCameraTakeover View;
	FVector Focus = FVector::ZeroVector;
	FVector PlayerOrigin = FVector::ZeroVector;
	FString DisplayName;
	float BaseHeight = 4000.f;
	float Zoom = 1.f;
	float ViewAge = 0.f;
	bool bPreviousCursor = false;
};
