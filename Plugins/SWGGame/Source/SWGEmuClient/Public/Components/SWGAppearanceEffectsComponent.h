#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TRE/SWGMeshReader.h"
#include "SWGAppearanceEffectsComponent.generated.h"

class USWGAppearanceBillboardComponent;

/**
 * The particle (.prt) and sprite (.spr) parts of a .cmp appearance, riding on the mesh the generator built for it:
 * lamp halos, laser bolt glows. Particles go to the world particle subsystem (only near the camera play), sprites
 * become USWGAppearanceBillboardComponents attached to the mesh.
 */
UCLASS(ClassGroup = "SWGEmu")
class SWGEMUCLIENT_API USWGAppearanceEffectsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/** Adds a component to Owner and builds Parts on MeshComponent (the owner's root). Returns null when Parts is empty. */
	static USWGAppearanceEffectsComponent* Create(AActor& Owner, USceneComponent& MeshComponent, const TArray<FSWGMeshEffectPart>& Parts);

	int32 GetEmitterCount() const { return EmitterIds.Num(); }
	int32 GetSpriteCount() const { return Sprites.Num(); }

private:
	void Build(USceneComponent& MeshComponent, const TArray<FSWGMeshEffectPart>& Parts);
	void ReleaseEmitters();

	TArray<int32> EmitterIds;

	UPROPERTY()
	TArray<TObjectPtr<USWGAppearanceBillboardComponent>> Sprites;
};
