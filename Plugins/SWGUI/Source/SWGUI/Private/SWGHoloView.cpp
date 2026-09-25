#include "SWGHoloView.h"
#include "SWGGameLayout.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SWGTangibleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

bool FSWGHoloView::FindProjectorLocation(APawn* Pawn, float Distance, float Height, FVector& OutLocation, float Side)
{
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	const FRotator Facing(0.f, Pawn->GetActorRotation().Yaw, 0.f);
	// The pawn's own origin isn't a ground reference; trace for the ground instead.
	OutLocation = Pawn->GetActorLocation() + Facing.RotateVector(FVector(Distance, Side, 0.f));
	FHitResult Ground;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SWGHoloGround), false, Pawn);
	if (World->LineTraceSingleByChannel(Ground, OutLocation + FVector(0.f, 0.f, 200.f), OutLocation - FVector(0.f, 0.f, 500.f), ECC_Visibility, Query))
	{
		OutLocation.Z = Ground.ImpactPoint.Z;
	}
	OutLocation.Z += Height;
	return true;
}

void FSWGHoloView::Begin(UUserWidget& Owner, const FVector& CameraLocation, const FVector& LookAt, float FieldOfView, float InBlendSeconds, float HudOpacity)
{
	APlayerController* PlayerController = Owner.GetOwningPlayer();
	UWorld* World = Owner.GetWorld();
	if (!PlayerController || !World || bActive)
	{
		return;
	}
	bActive = true;
	BlendSeconds = InBlendSeconds;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CameraLocation, (LookAt - CameraLocation).Rotation(), Params);
	if (Camera.IsValid())
	{
		Camera->GetCameraComponent()->SetFieldOfView(FieldOfView);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		PreviousViewTarget = PlayerController->GetViewTarget();
		PlayerController->SetViewTargetWithBlend(Camera.Get(), BlendSeconds, VTBlend_EaseInOut, 2.f);
	}
	PlayerController->SetIgnoreMoveInput(true);
	PlayerController->SetIgnoreLookInput(true);
	// From over the shoulder the player's own name hangs right across the view.
	if (USWGTangibleComponent* Tangible = PlayerController->GetPawn() ? PlayerController->GetPawn()->FindComponentByClass<USWGTangibleComponent>() : nullptr)
	{
		Tangible->SetNameLabelHidden(true);
	}
	// The HUD fades back so the hologram reads; the overlay's own controls stay solid.
	if (USWGGameLayout* Layout = USWGGameLayout::GetLayout(&Owner))
	{
		PreviousHudOpacity = Layout->GetRenderOpacity();
		Layout->SetRenderOpacity(HudOpacity);
	}
}

void FSWGHoloView::End(UUserWidget& Owner)
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	if (APlayerController* PlayerController = Owner.GetOwningPlayer())
	{
		AActor* Target = PreviousViewTarget.IsValid() ? PreviousViewTarget.Get() : PlayerController->GetPawn();
		if (Target && Camera.IsValid())
		{
			PlayerController->SetViewTargetWithBlend(Target, BlendSeconds, VTBlend_EaseInOut, 2.f);
		}
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
		if (USWGTangibleComponent* Tangible = PlayerController->GetPawn() ? PlayerController->GetPawn()->FindComponentByClass<USWGTangibleComponent>() : nullptr)
		{
			Tangible->SetNameLabelHidden(false);
		}
	}
	if (USWGGameLayout* Layout = USWGGameLayout::GetLayout(&Owner))
	{
		Layout->SetRenderOpacity(PreviousHudOpacity);
	}
	if (Camera.IsValid())
	{
		// Kept until the blend back has left it.
		Camera->SetLifeSpan(BlendSeconds + 0.2f);
		Camera = nullptr;
	}
}
