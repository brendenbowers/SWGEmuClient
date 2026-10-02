#pragma once

#include "SWGHoloMapAppearanceComponent.h"
#include "SWGHoloMapHologramAppearance.generated.h"

class UMaterialInstanceDynamic;

/** Blue hologram terrain and low-detail buildings. */
UCLASS()
class SWGUIHOLO_API USWGHoloMapHologramAppearance : public USWGHoloMapAppearanceComponent
{
	GENERATED_BODY()

public:
	virtual void ApplyTerrain() override;
	virtual void UpdateContourSpacing() override;
	virtual void StyleBuilding(UStaticMeshComponent* Building, UStaticMesh* Mesh, const UStaticMeshComponent* Source = nullptr) const override;

protected:
	virtual void OnRegister() override;

private:
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HoloMaterial;
};
