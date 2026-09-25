#pragma once

#include "CoreMinimal.h"
#include "SWGHoloProjectorActor.h"
#include "SWGHoloFigureActor.generated.h"

class ACharacter;
class UMeshComponent;
class USkeletalMeshComponent;

/**
 * A small hologram of a character: its body, worn clothing and held items,
 * copied in M_SWGHologram and following the real body's live pose (leader
 * pose). The droid hovers above and behind the figure's head; its rays land
 * on the head, shoulders and feet, found from the pose itself so any species
 * fits, and a scan line sweeps head to toe whenever the figure changes.
 *
 * Its +X faces the viewer. Copies are rebuilt when the source's gear changes.
 */
UCLASS(NotPlaceable)
class SWGUI_API ASWGHoloFigureActor : public ASWGHoloProjectorActor
{
	GENERATED_BODY()

public:
	ASWGHoloFigureActor();

	void SetSource(ACharacter* InSource);

	/** Turns the figure on its disc, degrees; zero faces the viewer. */
	void SetFigureYaw(float Degrees);
	float GetFigureYaw() const { return FigureYaw; }

	/** The copy standing in for Source (a body, wearable or held-item component of the source), if any. */
	UMeshComponent* FindCopy(const UPrimitiveComponent* Source) const;

	/** The body's copy; its bones follow the real body. */
	USkeletalMeshComponent* GetBodyCopy() const { return BodyCopy; }

	/** Top of the figure's head above the actor, world units. */
	float GetFigureHeight() const { return FigureTop; }

	/**
	 * Where on the figure an item in these equip slots ("chest1,chest2") sits,
	 * world space: its held-item copy if it has one, else the bone the first
	 * known slot hangs from, else mid-spine.
	 */
	FVector GetItemAnchor(const FString& SlotNames, const UPrimitiveComponent* ItemVisual) const;

	/** Makes one item's copy glow brighter than the rest; null clears it. */
	void SetHighlighted(const UPrimitiveComponent* ItemVisual);

	/** Size of the figure against the real character. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float FigureScale = 0.55f;

	/** How far above the head the droid hovers, world units. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float DroidClearance = 45.f;

	/** Distance the material keeps the figure lit before fading it; covers a tall species at FigureScale. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float FadeRadius = 400.f;

	/** Seconds between looks at the source for gear that came or went. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float SourceScanSeconds = 0.5f;

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual FVector GetDroidHover() const override;
	virtual FVector GetProjectionTarget(int32 RayIndex) const override;
	virtual FVector GetSweepTarget(int32 RayIndex, float Phase) const override;
	virtual float GetFadeRadius() const override { return FadeRadius; }

private:
	/** The source's visible meshes, as (component, mesh asset) pairs; a change means the copies are stale. */
	TArray<TPair<const UObject*, const UObject*>> SourceSignature() const;
	void Rebuild();
	void ClearCopies();
	/** Refreshes the bone positions the rays aim at, in actor space. */
	void UpdateShape();
	/** Widest bone either side within a height band, as a point at that band's middle; bRight picks +Y. */
	FVector SidePoint(float MinZ, float MaxZ, bool bRight) const;

	UPROPERTY()
	TObjectPtr<USceneComponent> FigureRoot;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> BodyCopy;

	/** Every copy, CopySources[i] being the component Copies[i] stands in for. */
	UPROPERTY()
	TArray<TObjectPtr<UMeshComponent>> Copies;
	TArray<TWeakObjectPtr<const UPrimitiveComponent>> CopySources;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FigureMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HighlightMaterial;

	TWeakObjectPtr<UMeshComponent> HighlightedCopy;

	/** A bone of the body copy, world space; false if it has no such bone. */
	bool GetBoneAnchor(FName Bone, FVector& OutWorld) const;

	TWeakObjectPtr<ACharacter> Source;
	TArray<TPair<const UObject*, const UObject*>> BuiltSignature;
	double NextSourceScanTime = 0.0;

	/** Bone positions in actor space, from the last UpdateShape. */
	TArray<FVector> BonePoints;
	float FigureTop = 100.f;
	float FigureBottom = 0.f;
	float HeadJointZ = 90.f;
	/** The toes have been stood on the disc; done once per build. */
	bool bGrounded = false;
	float FigureYaw = 0.f;
};
