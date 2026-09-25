#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWGHoloProjectorActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * What every hologram shares: a faint light disc under the image, a hovering
 * droid that projects it with a few rays (plus sweeping ones while the image
 * changes), the fade-in, and M_SWGHologram instances that follow the actor.
 * Subclasses say where the droid hovers and where its rays land.
 */
UCLASS(Abstract, NotPlaceable)
class SWGUI_API ASWGHoloProjectorActor : public AActor
{
	GENERATED_BODY()

public:
	ASWGHoloProjectorActor();

	/** Which side of the disc the droid hovers over (world XY); the far side from the viewer keeps it out of the way. */
	void SetDroidSide(const FVector2D& Direction);

	/**
	 * More rays from the droid's lens, to world points: for a second image the
	 * same droid projects. Brightness scales each against HoloIntensity. Each
	 * call replaces the last; an empty list hides them.
	 */
	void SetExtraRays(const TArray<FVector>& WorldTargets, const TArray<float>& Brightness);

	/** Where the projecting droid is, world space. */
	FVector GetDroidLocation() const;

	/** The hovering droid that projects the image; a mobile template, loaded through the item mesh path. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	FString DroidTemplate = TEXT("object/mobile/shared_training_remote.iff");

	/** Droid height above the disc, as a fraction of the disc's radius. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float DroidHeight = 0.5f;

	/** The training remote's model is about 10 cm across; scaled up so it reads at arm's length. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float DroidScale = 2.5f;

	/** How far past the centre toward DroidSide it hovers, as a fraction of the disc's radius. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float DroidReach = 0.75f;

	/** Disc diameter in world units. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float DiscDiameter = 240.f;

	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	FLinearColor HoloColor = FLinearColor(0.12f, 0.55f, 1.f);

	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float HoloIntensity = 0.7f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Droid position relative to the actor, before its bob. */
	virtual FVector GetDroidHover() const;

	/** Extra turn on the droid beyond facing the image's centre. */
	virtual float GetDroidExtraYaw() const { return 0.f; }

	/** Where projection ray RayIndex lands, relative to the actor. */
	virtual FVector GetProjectionTarget(int32 RayIndex) const;

	/** Where sweep ray RayIndex lands at Phase (0-1, repeating) of its pass. */
	virtual FVector GetSweepTarget(int32 RayIndex, float Phase) const;

	/** Turns the default rim targets, e.g. to follow content being dragged round. */
	virtual float GetRimAngleOffset() const { return 0.f; }

	/** How far from the actor the material keeps things lit before fading them. */
	virtual float GetFadeRadius() const { return DiscDiameter * 0.52f; }

	/** A point on the disc's rim, AngleDegrees round from DroidSide. */
	FVector RimPoint(float AngleDegrees) const;

	UMaterialInstanceDynamic* MakeHoloMaterial(const FLinearColor& Color, float Intensity);

	/** Shows the sweep rays for a moment; call whenever the image changes. */
	void NoteProjectionChange();

	/** Height of the image's base above the actor; the default droid and rim sit relative to it. */
	float ProjectionLift = 0.f;

	/** False for an image that is only part of another hologram: no disc, droid or rays of its own. Set in the constructor. */
	bool bHasProjector = true;

	/** Set in a subclass constructor; the rays are made in BeginPlay. */
	int32 ProjectionRayCount = 4;
	int32 SweepRayCount = 2;
	/** Ray thickness, world units. */
	float RayWidth = 0.7f;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BaseComponent;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> GlowLight;

	/** Bobs and turns in Tick; carries whichever mesh component the droid's template resolves to. */
	UPROPERTY()
	TObjectPtr<USceneComponent> DroidRoot;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> DroidLens;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> DroidLensLight;

	/** An engine cylinder, 100 units tall and across; rays and beams are scaled from it. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> BeamMesh;

	FVector2D DroidSide = FVector2D(1.f, 0.f);
	/** Seconds since the hologram appeared; drives the bob and any pulsing. */
	float DroidTime = 0.f;
	float FadeAlpha = 0.f;
	double LastProjectionChangeTime = 0.0;

private:
	void RequestDroid();
	void UpdateDroid();
	/** Stretches an engine cylinder from Origin to Target. */
	void PlaceRay(UStaticMeshComponent* Line, const FVector& Origin, const FVector& Target) const;

	/** Sparse projection rays from the droid to the image. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ProjectionLines;

	/** Extra rays that sweep the image while it is changing. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SweepLines;

	/** SetExtraRays' lines, made as needed and hidden when unused. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ExtraLines;
	TArray<FVector> ExtraTargets;
	TArray<float> ExtraBrightness;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> HoloMaterial;

	/** Every hologram material this actor made; their Center/Fade follow the actor each tick. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> HoloMaterials;

	float SweepStartTime = 0.f;
};
