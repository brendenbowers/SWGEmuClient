#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGHoloMapWidget.generated.h"

class ACameraActor;
class ASWGHoloMapActor;
class UButton;
class UTextBlock;
class USWGWaypointSubsystem;

DECLARE_MULTICAST_DELEGATE(FSWGOnHoloMapClosed);
DECLARE_MULTICAST_DELEGATE(FSWGOnHoloMapSwitchToWindow);

/**
 * The holographic planet map: projects an ASWGHoloMapActor of the ground
 * around the player in front of them, swings the camera over their shoulder
 * onto it, and turns the screen into its controls. Left-drag pans, right-
 * drag turns, the wheel zooms, a double-click sets a waypoint. Closing puts
 * the camera and controls back.
 *
 * Builds its own hint bar when used from C++; a Blueprint subclass may lay
 * out its own, binding the optional widgets below.
 */
UCLASS(Blueprintable)
class SWGUI_API USWGHoloMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloMap")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloMap")
	void CenterOnPlayer();

	/** Asks the owner to replace this with the window map. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloMap")
	void SwitchToWindow();

	FSWGOnHoloMapClosed OnClosed;
	FSWGOnHoloMapSwitchToWindow OnSwitchToWindow;

	/** World units in front of the player the projector stands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloMap")
	float ProjectorDistance = 170.f;

	/** How much of the HUD stays visible while the hologram is up, 0-1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloMap", meta = (ClampMin = "0", ClampMax = "1"))
	float HudOpacity = 0.12f;

	/** World units above the ground the projector's disc floats: about table height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloMap")
	float ProjectorHeight = 95.f;

	/** Metres from centre to rim when it opens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloMap")
	float StartRadius = 600.f;

	/** Over-the-shoulder camera, relative to the hologram's centre in the player's frame: back, right, up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloMap")
	FVector CameraOffset = FVector(-250.f, 100.f, 168.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloMap")
	float CameraBlendSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloMap")
	bool bCreateWaypointOnDoubleClick = true;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CenterButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> WindowButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

private:
	void Project();
	void RestoreView();
	/** Where the mouse's ray meets the hologram, raw metres. */
	bool MouseToRaw(FVector2D& OutRaw) const;
	/** Same for any viewport pixel. */
	bool ScreenToRaw(const FVector2D& ViewportPosition, FVector2D& OutRaw) const;
	/** Moves the view by the analog sticks and triggers. */
	void ApplyAnalog(float DeltaSeconds);
	void Zoom(float Factor);
	FString GetPlanetName() const;

	UFUNCTION() void RefreshWaypoints();
	UFUNCTION() void HandleCenterClicked();
	UFUNCTION() void HandleWindowClicked();
	UFUNCTION() void HandleCloseClicked();

	UPROPERTY()
	TObjectPtr<ASWGHoloMapActor> Hologram;

	UPROPERTY()
	TObjectPtr<ACameraActor> ShoulderCamera;

	UPROPERTY()
	TObjectPtr<AActor> PreviousViewTarget;

	UPROPERTY()
	TObjectPtr<USWGWaypointSubsystem> Waypoints;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> ButtonTextTints;

	float PreviousHudOpacity = 1.f;
	FVector2D LastMouse = FVector2D::ZeroVector;
	/** Where the cursor would be, in viewport pixels, moved by raw deltas while a drag has it captured. */
	FVector2D VirtualCursor = FVector2D::ZeroVector;
	bool bHasVirtualCursor = false;
	/** Latest analog readings, applied every tick: left stick pans, right stick turns, triggers zoom. */
	FVector2D LeftStick = FVector2D::ZeroVector;
	FVector2D RightStick = FVector2D::ZeroVector;
	float LeftTrigger = 0.f;
	float RightTrigger = 0.f;
	bool bPanning = false;
	bool bTurning = false;
	bool bClosing = false;
};
