#include "UI/SWGCharacterPreviewLayout.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"

namespace
{
	/** Engine's BasicShapes/Cube is 100 units per side, so scale = desired extent / 100. */
	constexpr float EngineCubeSize = 100.f;

	/** Slight oversize so no seam shows at the frame edge from rounding/rotation. */
	constexpr float BackdropCoverMargin = 1.02f;
}

namespace SWGCharacterPreview
{

FVector2D GetViewportSize()
{
	FVector2D ViewportSize = FVector2D::ZeroVector;
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	return ViewportSize;
}

void FitBackdropToCamera(UWorld& World)
{
	TArray<AActor*> Backdrops;
	UGameplayStatics::GetAllActorsOfClassWithTag(&World, AStaticMeshActor::StaticClass(), TEXT("CharacterPreviewBackdrop"), Backdrops);
	AStaticMeshActor* Backdrop = Backdrops.IsEmpty() ? nullptr : Cast<AStaticMeshActor>(Backdrops[0]);
	if (!Backdrop)
	{
		return;
	}

	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsOfClassWithTag(&World, ACameraActor::StaticClass(), TEXT("CharacterPreviewCamera"), Cameras);
	ACameraActor* Camera = Cameras.IsEmpty() ? nullptr : Cast<ACameraActor>(Cameras[0]);
	UCameraComponent* CameraComponent = Camera ? Camera->GetCameraComponent() : nullptr;
	if (!CameraComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("SWGCharacterPreview: no CharacterPreviewCamera to fit the backdrop to"));
		return;
	}
	CameraComponent->SetConstraintAspectRatio(false);

	const FVector2D ViewportSize = GetViewportSize();
	if (ViewportSize.X <= 0.f || ViewportSize.Y <= 0.f)
	{
		return;
	}

	const FVector CameraLocation = CameraComponent->GetComponentLocation();
	const FRotator CameraRotation = CameraComponent->GetComponentRotation();
	const FVector CameraForward = CameraRotation.Vector();

	const float Distance = FVector::DotProduct(Backdrop->GetActorLocation() - CameraLocation, CameraForward);
	if (Distance <= 1.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("SWGCharacterPreview: backdrop is behind/on the preview camera (distance %.1f) - not fitting"), Distance);
		return;
	}

	// FieldOfView is the HORIZONTAL fov, but only at the camera's own
	// AspectRatio - what the renderer keeps constant when the viewport is a
	// different shape depends on the aspect-ratio axis constraint, and the
	// engine default (AspectRatio_MaintainYFOV) holds the VERTICAL fov and
	// widens horizontally. Assuming the horizontal fov stays put undersizes the
	// backdrop badly on a wide viewport. Size for whichever constraint yields
	// the larger extent - covering is harmless, coming up short isn't.
	const float ViewportAspect = ViewportSize.X / ViewportSize.Y;
	const float CameraAspect = CameraComponent->AspectRatio > 0.f ? CameraComponent->AspectRatio : ViewportAspect;
	const float TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(CameraComponent->FieldOfView * 0.5f));
	const float TanHalfVertical = TanHalfHorizontal / CameraAspect;

	const float HalfWidthMaintainX = Distance * TanHalfHorizontal;
	const float HalfHeightMaintainX = HalfWidthMaintainX / ViewportAspect;
	const float HalfHeightMaintainY = Distance * TanHalfVertical;
	const float HalfWidthMaintainY = HalfHeightMaintainY * ViewportAspect;

	const float HalfWidth = FMath::Max(HalfWidthMaintainX, HalfWidthMaintainY);
	const float HalfHeight = FMath::Max(HalfHeightMaintainX, HalfHeightMaintainY);

	// Cover rather than stretch: one uniform extent keeps the (square) source
	// image undistorted and crops the overflow, instead of squashing it to the
	// window's aspect.
	const float Extent = FMath::Max(HalfWidth, HalfHeight) * 2.f * BackdropCoverMargin;
	const float Scale = Extent / EngineCubeSize;

	// AStaticMeshActor defaults to Static mobility, and UE silently ignores
	// SetActorLocation/SetActorRotation on static components at runtime - the
	// scale would still apply, leaving the quad correctly sized but never moved
	// onto the view axis or turned to face the pitched camera.
	if (UStaticMeshComponent* MeshComponent = Backdrop->GetStaticMeshComponent())
	{
		if (MeshComponent->Mobility != EComponentMobility::Movable)
		{
			MeshComponent->SetMobility(EComponentMobility::Movable);
		}
	}

	// Matching the camera's rotation leaves the cube's -X face (the one the
	// camera already sees) square-on to the view, including its pitch.
	Backdrop->SetActorRotation(CameraRotation);
	Backdrop->SetActorLocation(CameraLocation + CameraForward * Distance);
	Backdrop->SetActorScale3D(FVector(Backdrop->GetActorScale3D().X, Scale, Scale));
}

}
