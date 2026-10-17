#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SWGAppearanceBillboardComponent.generated.h"

struct FSWGBeam;
struct FSWGSprite;
class UPrimitiveComponent;

/**
 * A retail SpriteAppearance (.spr: a camera-facing square) or BeamAppearance (appearance/beam_*.iff: a flat strip along
 * +Z that turns about its own axis to face the camera). The sprite draws through the particle billboard material of
 * its shader; the beam through the ribbon material (its texture runs along the length, which the particle quad cannot).
 * Place the component where the appearance's origin goes (a .cmp PART transform).
 */
UCLASS(ClassGroup = (SWG))
class SWGEFFECTS_API USWGAppearanceBillboardComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USWGAppearanceBillboardComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Builds the quad for a sprite; Scale is the appearance scale. False when the effects subsystem or shader is unavailable. */
	bool SetupSprite(const FSWGSprite& Sprite, float Scale = 1.f);

	/** Builds the strip for a beam; Scale multiplies length and width. */
	bool SetupBeam(const FSWGBeam& Beam, float Scale = 1.f);

	/** Side of the square (SpriteAppearance 0x789af0: the stored radius is the bounding sphere, the side is sqrt(2) x radius x scale). */
	static float ComputeSpriteSideMeters(float BoundsRadius, float Scale);

	/**
	 * Local Y (the strip's normal) for a beam along AxisDirection seen from ViewerOffset (viewer minus beam middle):
	 * the part of the offset perpendicular to the axis, or a fixed fallback when the viewer is on the axis.
	 */
	static FVector ComputeBeamNormal(const FVector& AxisDirection, const FVector& ViewerOffset);

	bool IsBeam() const { return bBeam; }

private:
	bool ResolveViewerLocation(FVector& OutLocation) const;

	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> Render;
	bool bBeam = false;
};
