#pragma once

#include "CoreMinimal.h"

class ACameraActor;
class APawn;
class UUserWidget;

/**
 * What every full-screen hologram does to the view while it is up: swings to
 * its own camera, holds the pawn still, hides the player's name label and
 * fades the HUD back. End puts it all back and is safe to call twice.
 */
class SWGUI_API FSWGHoloView
{
public:
	/** In front of the pawn at Distance (Side to its right), Height above the ground found there. */
	static bool FindProjectorLocation(APawn* Pawn, float Distance, float Height, FVector& OutLocation, float Side = 0.f);

	void Begin(UUserWidget& Owner, const FVector& CameraLocation, const FVector& LookAt, float FieldOfView, float BlendSeconds, float HudOpacity);
	void End(UUserWidget& Owner);

	ACameraActor* GetCamera() const { return Camera.Get(); }

private:
	TWeakObjectPtr<ACameraActor> Camera;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	float PreviousHudOpacity = 1.f;
	float BlendSeconds = 0.f;
	bool bActive = false;
};
