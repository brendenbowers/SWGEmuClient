#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWGHoloMapPlacementLayer.generated.h"

class UDynamicMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;

/** Placement-only visuals attached to a holo map's baked terrain. */
UCLASS()
class SWGUIHOLO_API USWGHoloMapPlacementLayer : public UActorComponent
{
	GENERATED_BODY()

public:
	struct FNoBuildArea
	{
		FBox2D Rect = FBox2D(ForceInit);
		FVector2D Center = FVector2D::ZeroVector;
		float Radius = 0.f;
		bool bCircle = false;
		bool bBlocking = false;
		bool bPlacing = false;
		FLinearColor Tint = FLinearColor::White;
	};

	void SetGhost(UStaticMesh* Mesh, const FVector2D& Raw, const FQuat& WorldRotation, const FLinearColor& Tint);
	void SetNoBuildAreas(const TArray<FNoBuildArea>& Areas);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	void RebuildNoBuildAreas();
	void RefreshAfterBake();

	UPROPERTY() TObjectPtr<UStaticMeshComponent> GhostComponent;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GhostMaterial;
	UPROPERTY() TObjectPtr<UDynamicMeshComponent> NoBuildComponent;
	UPROPERTY() TObjectPtr<UDynamicMeshComponent> BlockingComponent;
	UPROPERTY() TObjectPtr<UDynamicMeshComponent> PlacingComponent;
	TArray<FNoBuildArea> NoBuildAreas;
	uint32 NoBuildHash = 0;
	TWeakObjectPtr<UStaticMesh> GhostMesh;
	FVector2D GhostRaw = FVector2D::ZeroVector;
	FQuat GhostRotation = FQuat::Identity;
	FLinearColor GhostTint = FLinearColor::White;
};
