#pragma once

#include "CoreMinimal.h"
#include "Components/DynamicMeshComponent.h"
#include "SWGTrailComponent.generated.h"

/**
 * A weapon swing's ribbon: each frame the two edge points (a blade's trail hardpoints) are sampled in world space
 * and joined into a strip that fades over the .swh lifetime. Textured by the swoosh's shader through the
 * M_SWGRibbon masters (UV0 along/across the strip, UV1.x the fade).
 */
UCLASS(ClassGroup = "SWGEmu")
class SWGEMUCLIENT_API USWGTrailComponent : public UDynamicMeshComponent
{
	GENERATED_BODY()

public:
	USWGTrailComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** BaseMaterial is a shared ribbon material; the component makes its own instance to tint. */
	void Configure(UMaterialInterface* BaseMaterial, const FLinearColor& Tint, float LifetimeSeconds);

	/** The two points whose world positions form the ribbon's edges. */
	void SetEdge(USceneComponent* InStart, USceneComponent* InEnd);

	/** On: samples every frame. Off: the existing ribbon fades out over its lifetime. */
	void SetEmitting(bool bInEmitting);
	bool IsEmitting() const { return bEmitting; }

	/** Brightness of a ribbon sample of the given age: 1 fresh, 0 at the end of its life. */
	static float ComputeFade(float AgeSeconds, float LifetimeSeconds);

	/** Samples held right now (tests, diagnostics). */
	int32 GetSampleCount() const { return Samples.Num(); }

private:
	struct FSample
	{
		FVector StartLocation = FVector::ZeroVector;
		FVector EndLocation = FVector::ZeroVector;
		double Time = 0.0;
	};

	void RebuildMesh(double Now);

	TWeakObjectPtr<USceneComponent> StartPoint;
	TWeakObjectPtr<USceneComponent> EndPoint;
	TArray<FSample> Samples;
	float LifetimeSeconds = 0.1f;
	bool bEmitting = false;
	bool bMeshEmpty = true;
};
