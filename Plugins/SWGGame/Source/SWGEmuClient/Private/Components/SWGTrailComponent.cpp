#include "Components/SWGTrailComponent.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

using namespace UE::Geometry;

namespace
{
	// A ribbon needs two samples to span anything; below this lifetime a 30 fps frame would never provide them.
	constexpr float MinLifetimeSeconds = 0.08f;
	const FName TintParameter(TEXT("Tint"));
}

USWGTrailComponent::USWGTrailComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	SetAbsolute(true, true, true);
}

void USWGTrailComponent::Configure(UMaterialInterface* BaseMaterial, const FLinearColor& Tint, float InLifetimeSeconds)
{
	LifetimeSeconds = FMath::Max(InLifetimeSeconds, MinLifetimeSeconds);
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Instance->SetVectorParameterValue(TintParameter, Tint);
		SetMaterial(0, Instance);
	}
}

void USWGTrailComponent::SetEdge(USceneComponent* InStart, USceneComponent* InEnd)
{
	StartPoint = InStart;
	EndPoint = InEnd;
}

void USWGTrailComponent::SetEmitting(bool bInEmitting)
{
	bEmitting = bInEmitting;
	if (bInEmitting)
	{
		SetComponentTickEnabled(true);
	}
}

float USWGTrailComponent::ComputeFade(float AgeSeconds, float InLifetimeSeconds)
{
	return InLifetimeSeconds > 0.f ? FMath::Clamp(1.f - AgeSeconds / InLifetimeSeconds, 0.f, 1.f) : 0.f;
}

void USWGTrailComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	if (bEmitting && StartPoint.IsValid() && EndPoint.IsValid())
	{
		FSample& Sample = Samples.AddDefaulted_GetRef();
		Sample.StartLocation = StartPoint->GetComponentLocation();
		Sample.EndLocation = EndPoint->GetComponentLocation();
		Sample.Time = Now;
	}

	while (!Samples.IsEmpty() && Now - Samples[0].Time > LifetimeSeconds)
	{
		Samples.RemoveAt(0);
	}

	if (Samples.Num() >= 2)
	{
		RebuildMesh(Now);
		return;
	}

	if (!bMeshEmpty)
	{
		SetMesh(FDynamicMesh3());
		bMeshEmpty = true;
	}
	if (!bEmitting)
	{
		SetComponentTickEnabled(false);
	}
}

void USWGTrailComponent::RebuildMesh(double Now)
{
	FDynamicMesh3 Mesh;
	Mesh.EnableAttributes();
	Mesh.Attributes()->SetNumUVLayers(2);
	FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
	FDynamicMeshUVOverlay* AlongUVs = Mesh.Attributes()->GetUVLayer(0);
	FDynamicMeshUVOverlay* FadeUVs = Mesh.Attributes()->GetUVLayer(1);

	const int32 SampleCount = Samples.Num();
	TArray<int32> VertexIds, NormalIds, AlongIds, FadeIds;
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const FSample& Sample = Samples[SampleIndex];
		// Newest sample at u = 0, oldest at u = 1, so the texture's bright end leads the swing.
		const float AlongU = 1.f - static_cast<float>(SampleIndex) / (SampleCount - 1);
		const float Fade = ComputeFade(static_cast<float>(Now - Sample.Time), LifetimeSeconds);
		for (int32 Edge = 0; Edge < 2; ++Edge)
		{
			VertexIds.Add(Mesh.AppendVertex(Edge == 0 ? Sample.StartLocation : Sample.EndLocation));
			NormalIds.Add(Normals->AppendElement(FVector3f::UpVector));
			AlongIds.Add(AlongUVs->AppendElement(FVector2f(AlongU, static_cast<float>(Edge))));
			FadeIds.Add(FadeUVs->AppendElement(FVector2f(Fade, 0.f)));
		}
	}

	auto AddTriangle = [&](int32 First, int32 Second, int32 Third)
	{
		const int32 TriangleId = Mesh.AppendTriangle(VertexIds[First], VertexIds[Second], VertexIds[Third]);
		if (TriangleId >= 0)
		{
			Normals->SetTriangle(TriangleId, FIndex3i(NormalIds[First], NormalIds[Second], NormalIds[Third]));
			AlongUVs->SetTriangle(TriangleId, FIndex3i(AlongIds[First], AlongIds[Second], AlongIds[Third]));
			FadeUVs->SetTriangle(TriangleId, FIndex3i(FadeIds[First], FadeIds[Second], FadeIds[Third]));
		}
	};

	for (int32 SampleIndex = 0; SampleIndex + 1 < SampleCount; ++SampleIndex)
	{
		const int32 Near = SampleIndex * 2;
		const int32 Far = Near + 2;
		AddTriangle(Near, Far, Near + 1);
		AddTriangle(Near + 1, Far, Far + 1);
	}

	SetMesh(MoveTemp(Mesh));
	bMeshEmpty = false;
}
