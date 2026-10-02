#pragma once

#include "SWGHoloMapAppearanceComponent.h"
#include "SWGHoloMapRealisticAppearance.generated.h"

class ASceneCapture2D;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;

/** Real ground colour and textured buildings under a faint hologram overlay. */
UCLASS()
class SWGUI_API USWGHoloMapRealisticAppearance : public USWGHoloMapAppearanceComponent
{
	GENERATED_BODY()

public:
	virtual void ApplyTerrain() override;
	virtual void UpdateContourSpacing() override;
	virtual void StyleBuilding(UStaticMeshComponent* Building, UStaticMesh* Mesh, const UStaticMeshComponent* Source = nullptr) const override;
	virtual float BuildingScale(float Default) const override { return 1.f; }
	virtual float HeightExaggeration(float Default) const override { return 1.3f; }

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	void CaptureTerrainColor();
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> OverlayMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TerrainMapMaterial;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> TerrainColorTarget;
	UPROPERTY() TObjectPtr<ASceneCapture2D> TerrainColorCapture;
};
