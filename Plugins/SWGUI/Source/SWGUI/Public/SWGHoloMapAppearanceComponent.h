#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWGHoloMapAppearanceComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/** The one active terrain and building appearance on a holo map. */
UCLASS(Abstract)
class SWGUI_API USWGHoloMapAppearanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	virtual void ApplyTerrain() {}
	virtual void UpdateContourSpacing() {}
	virtual void StyleBuilding(UStaticMeshComponent* Building, UStaticMesh* Mesh, const UStaticMeshComponent* Source = nullptr) const {}
	virtual float BuildingScale(float Default) const { return Default; }
	virtual float HeightExaggeration(float Default) const { return Default; }
};
