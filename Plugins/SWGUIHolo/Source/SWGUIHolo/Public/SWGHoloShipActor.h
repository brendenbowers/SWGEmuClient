#pragma once

#include "CoreMinimal.h"
#include "SWGHoloProjectorActor.h"
#include "SWGHoloShipActor.generated.h"

class UMaterialInterface;
class UStaticMeshComponent;

/**
 * Copies a ship's visible hull meshes under Parent, shrunk to fit FitRadius around Parent's origin, drawn with Material. The
 * source's mesh components can arrive after it spawns, so callers compare Signature with the one they built from and rebuild on a change.
 */
namespace SWGHoloShipCopy
{
	using FSignature = TArray<TPair<const UObject*, const UObject*>>;

	/** (component, mesh) of each static mesh component of Source that would be copied. */
	SWGUIHOLO_API FSignature Signature(const AActor* Source);

	/**
	 * Makes the copies, owned by Owner and attached to Parent; appends them to OutCopies. Returns the scale applied to the source
	 * (FitRadius over its bounding radius), or 0 when it has nothing to copy.
	 */
	SWGUIHOLO_API float Build(AActor& Owner, USceneComponent& Parent, const AActor& Source, UMaterialInterface* Material, float FitRadius, TArray<TObjectPtr<UStaticMeshComponent>>& OutCopies);

	SWGUIHOLO_API void Clear(TArray<TObjectPtr<UStaticMeshComponent>>& Copies);
}

/**
 * The target ship as a hologram: a small copy of its hull turning slowly on a projector disc, drawn in M_SWGHologram. Authored at
 * a nominal size; the piloting HUD scales the whole actor to fill its screen region.
 */
UCLASS(NotPlaceable)
class SWGUIHOLO_API ASWGHoloShipActor : public ASWGHoloProjectorActor
{
	GENERATED_BODY()

public:
	/** Radius of the hull hologram at actor scale 1, world units. */
	static constexpr float NominalRadius = 40.f;

	ASWGHoloShipActor();

	/** The ship to copy; null clears the hologram. The copy follows the source's meshes as they load. */
	void SetSource(const AActor* InSource);
	const AActor* GetSource() const { return Source.Get(); }

	int32 GetCopyCount() const { return Copies.Num(); }
	float GetSpinYaw() const { return SpinYaw; }

	/** Degrees per second the hull turns. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float SpinRate = 22.f;

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual float GetFadeRadius() const override { return NominalRadius * 3.f; }

private:
	void Rebuild();

	UPROPERTY()
	TObjectPtr<USceneComponent> HullRoot;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Copies;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HullMaterial;

	TWeakObjectPtr<const AActor> Source;
	SWGHoloShipCopy::FSignature BuiltSignature;
	double NextScanTime = 0.0;
	float SpinYaw = 0.f;
};
