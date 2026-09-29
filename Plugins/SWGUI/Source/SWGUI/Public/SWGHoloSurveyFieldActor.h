#pragma once

#include "CoreMinimal.h"
#include "SWGHoloProjectorActor.h"
#include "Subsystems/SWGSurveySubsystem.h"
#include "SWGHoloSurveyFieldActor.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
class UTextRenderComponent;

/**
 * A survey's concentrations projected onto the real ground around where it
 * was taken, at full scale: the density field draped over the terrain in
 * colour bands, a pillar on each grid point as tall as its concentration with
 * the percentage on top, and a tall beam and ripple on the best point (where
 * the server put the waypoint). Reveals outward from the centre like a scan
 * and can fade itself out. Part of the survey hologram; the projector droid
 * belongs to ASWGHoloSurveyActor.
 */
UCLASS(NotPlaceable)
class SWGUI_API ASWGHoloSurveyFieldActor : public ASWGHoloProjectorActor
{
	GENERATED_BODY()

public:
	ASWGHoloSurveyFieldActor();

	/** Builds the field for Result; the terrain is sampled on a worker and it appears when that lands. */
	void SetResult(const FSWGSurveyResult& Result);

	bool HasField() const { return bBuilt; }

	/** World position of a corner of the surveyed square (0-3), at ground level. */
	FVector GetCornerLocation(int32 Corner) const;

	/** World position of the best point's ground, if the survey found one worth a waypoint. */
	bool GetBestLocation(FVector& OutLocation) const;

	/** The field's middle and its half-width, world units, for framing a camera. */
	FVector GetFieldCenter() const { return GetActorLocation(); }
	float GetFieldHalfExtent() const { return RangeUnits() * 0.5f; }

	/** Pulls the field inward over Seconds, then removes it. */
	void FadeOutAndDestroy(float Seconds);

	/** Samples per side of the draped field. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Survey")
	int32 FieldResolution = 33;

	/** How far above the ground the field floats, metres. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Survey")
	float FieldLiftMetres = 0.6f;

	/** Low markers keep the field readable from beside the player. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Survey")
	float PillarHeightPerRange = 0.013f;

	UPROPERTY(EditAnywhere, Category = "SWGEmu|Survey")
	float RevealSeconds = 1.8f;

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual float GetFadeRadius() const override { return 1.e7f; }

private:
	struct FPillar
	{
		FVector Ground = FVector::ZeroVector;
		float Height = 0.f;
		float DistanceFromCenter = 0.f;
		TObjectPtr<UStaticMeshComponent> Beam;
		TObjectPtr<UTextRenderComponent> Label;
	};

	struct FFieldBake
	{
		/** FieldResolution^2 heights, rows running north from the south edge. */
		TArray<float> FieldHeights;
		/** One per survey sample, in the result's order. */
		TArray<float> SampleHeights;
		float CenterHeight = 0.f;
	};

	void ApplyBake(const FFieldBake& Bake);
	void ClearField();
	/** The survey's side length in world units. */
	float RangeUnits() const;

	UPROPERTY()
	TArray<TObjectPtr<UDynamicMeshComponent>> BandMeshes;

	/** Keeps pillars, labels and materials alive; FPillar isn't reflected. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> FieldObjects;

	/** Materials whose reveal radius and fade this actor drives itself. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FieldMaterials;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FieldPaintMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FieldPaintInstance;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BestBeam;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BestPing;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BestPingMaterial;

	TArray<FPillar> Pillars;
	FSWGSurveyResult Result;
	FVector BestGround = FVector::ZeroVector;
	FVector CornerGround[4];
	bool bHasBest = false;
	bool bBuilt = false;
	int32 BakeGeneration = 0;
	float RevealTime = 0.f;
	float FadeOutSeconds = 0.f;
	float FadeOutRemaining = -1.f;
};
