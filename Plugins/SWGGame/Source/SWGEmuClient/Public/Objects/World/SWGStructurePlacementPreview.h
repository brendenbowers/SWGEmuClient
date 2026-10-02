#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TRE/SWGFootprintReader.h"
#include "Subsystems/SWGStructurePlacementSubsystem.h"
#include "SWGStructurePlacementPreview.generated.h"

class UStaticMeshComponent;
class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USWGMeshGeneratorSubsystem;
class USWGTerrainSubsystem;
class USWGObjectGraphSubsystem;

/** Transient placement ghost, footprint cells and server-range guide. */
UCLASS(NotPlaceable)
class SWGEMUCLIENT_API ASWGStructurePlacementPreview : public AActor
{
	GENERATED_BODY()
public:
	ASWGStructurePlacementPreview();
	virtual void Tick(float DeltaSeconds) override;
	void Initialize(const FString& TemplatePath, USWGMeshGeneratorSubsystem* Meshes, USWGTerrainSubsystem* InTerrain,
		USWGObjectGraphSubsystem* InObjects, const USWGStructurePlacementSubsystem* InPlacement);
	void UpdatePlacement(const FVector2D& RawCenter, int32 Rotation, const FSWGStructureFootprint& Footprint,
		float ClearFloraRadius, bool bSnapToTerrain, ESWGPlacementVerdict Verdict);
	/** Keeps only the footprint on screen, in a neutral colour, for a placement that has been sent to the server. */
	void ShowAsMarker();
	FVector2D GetCenter() const { return Center; }
	UStaticMeshComponent* GetGhostComponent() const { return Ghost; }
	/** Hides the ghost, grids and guide lines while another view (the holo map) shows the placement. */
	void SetPresentationHidden(bool bHide);
	/** World preview: show only the projected structure, without placement guides. */
	void SetHologramOnly(bool bEnable);
	/** Reveal the original textured mesh from the base upward while the hologram recedes. */
	void BeginConstructionPrint();
	void SetConstructionProgress(float Progress);

private:
	void SetupConstructionMaterials();
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ghost;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Printed;
	UPROPERTY() TObjectPtr<UDynamicMeshComponent> BodyGrid;
	UPROPERTY() TObjectPtr<UDynamicMeshComponent> ReservedGrid;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GhostMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ConstructionHoloMaterial;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> PrintedMaterials;
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> SourceMaterials;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ReservedMaterial;
	UPROPERTY() TObjectPtr<USWGTerrainSubsystem> Terrain;
	UPROPERTY() TObjectPtr<USWGObjectGraphSubsystem> Objects;
	TWeakObjectPtr<const USWGStructurePlacementSubsystem> Placement;
	FVector2D Center = FVector2D::ZeroVector;
	FSWGStructureFootprint CurrentFootprint;
	int32 Angle = 0;
	bool bMarkerOnly = false;
	bool bPresentationHidden = false;
	bool bHologramOnly = false;
	bool bPrinting = false;
	float ConstructionProgress = 0.f;
	ESWGPlacementVerdict CurrentVerdict = ESWGPlacementVerdict::Invalid;
};
