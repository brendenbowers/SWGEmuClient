#pragma once

#include "CoreMinimal.h"
#include "SWGHoloProjectorActor.h"
#include "SWGSpaceHudModel.h"
#include "SWGHoloShipActor.h"
#include "SWGHoloRadarActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMeshComponent;

/** How a blip is drawn: one instanced mesh per class, each with its own glow colour. */
enum class ESWGRadarBlipClass : uint8 { Neutral, Friendly, Hostile, Structure, Target, Count };

/**
 * The 3D radar: a hologram sphere with an equatorial plane of range rings, a miniature of the pilot's ship at its centre and a blip
 * for every object in range at its true bearing, each with a stalk down (or up) to the plane. Ahead is the actor's +X; the piloting
 * HUD rotates the actor with the ship and scales it to its screen region. Authored at a nominal size.
 */
UCLASS(NotPlaceable)
class SWGUIHOLO_API ASWGHoloRadarActor : public ASWGHoloProjectorActor
{
	GENERATED_BODY()

public:
	static constexpr float NominalRadius = 50.f;
	/** Height of the sphere's centre above the projector disc at actor scale 1. */
	static constexpr float CentreLift = NominalRadius * 1.2f;

	ASWGHoloRadarActor();

	/** The ship whose miniature sits at the centre; null removes it. */
	void SetShip(const AActor* InShip);

	/** Places a blip for each entry (Relative in the ship's frame, Range the distance that reaches the sphere's surface). */
	void SetContents(const TArray<FSWGSpaceHudBlip>& InBlips, float InRange, const TOptional<FVector>& InNorth);

	/** The blip's centre in the actor's frame relative to the sphere's centre. */
	FVector GetBlipPosition(int32 Index) const { return BlipPositions.IsValidIndex(Index) ? BlipPositions[Index] : FVector::ZeroVector; }
	int32 GetBlipCount() const { return BlipPositions.Num(); }
	int32 GetBlipCount(ESWGRadarBlipClass BlipClass) const;
	/** The class an entry drew with. */
	ESWGRadarBlipClass GetBlipClass(int32 Index) const { return BlipClasses.IsValidIndex(Index) ? BlipClasses[Index] : ESWGRadarBlipClass::Neutral; }
	int32 GetStalkCount() const;
	bool HasMiniature() const { return MiniatureCopies.Num() > 0; }
	bool IsNorthMarkerShown() const;
	bool AreMaterialsReady() const { return bMaterialsReady; }

	/** World position of the sphere's centre. */
	FVector GetCentreLocation() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float GetFadeRadius() const override { return NominalRadius * 3.f; }

private:
	void EnsureMaterials();
	void RefreshBlips();
	void RebuildMiniature();
	UMaterialInstanceDynamic* MakeRadarMaterial(float Mode, const FLinearColor& Colour, float Intensity);

	UPROPERTY()
	TObjectPtr<USceneComponent> SphereRoot;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Shell;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Plane;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> HeadingMarker;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> NorthMarker;
	UPROPERTY()
	TObjectPtr<USceneComponent> MiniatureRoot;
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> BlipMeshes;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> StalkMesh;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> MiniatureCopies;
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> RadarMaterials;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MiniatureMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> RadarParent;

	TWeakObjectPtr<const AActor> Ship;
	SWGHoloShipCopy::FSignature MiniatureSignature;
	double NextShipScanTime = 0.0;

	TArray<FSWGSpaceHudBlip> Blips;
	TArray<FVector> BlipPositions;
	TArray<ESWGRadarBlipClass> BlipClasses;
	float Range = 0.f;
	TOptional<FVector> North;
	bool bMaterialsReady = false;
};
