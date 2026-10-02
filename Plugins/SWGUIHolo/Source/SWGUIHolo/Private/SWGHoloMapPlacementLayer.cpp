#include "SWGHoloMapPlacementLayer.h"
#include "SWGHoloMapActor.h"
#include "Common/SWGWorldScale.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

void USWGHoloMapPlacementLayer::OnRegister()
{
	Super::OnRegister();
	if (ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner()))
	{
		Map->OnBakeUpdated.AddUObject(this, &USWGHoloMapPlacementLayer::RefreshAfterBake);
		RefreshAfterBake();
	}
}

void USWGHoloMapPlacementLayer::OnUnregister()
{
	if (ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner())) { Map->OnBakeUpdated.RemoveAll(this); }
	if (IsValid(GhostComponent)) { GhostComponent->DestroyComponent(); }
	for (UDynamicMeshComponent* Component : { NoBuildComponent.Get(), BlockingComponent.Get(), PlacingComponent.Get() })
	{
		if (IsValid(Component)) { Component->DestroyComponent(); }
	}
	Super::OnUnregister();
}

void USWGHoloMapPlacementLayer::RefreshAfterBake()
{
	RebuildNoBuildAreas();
	SetGhost(GhostMesh.Get(), GhostRaw, GhostRotation, GhostTint);
}

void USWGHoloMapPlacementLayer::SetGhost(UStaticMesh* Mesh, const FVector2D& Raw, const FQuat& WorldRotation, const FLinearColor& Tint)
{
	GhostMesh = Mesh;
	GhostRaw = Raw;
	GhostRotation = WorldRotation;
	GhostTint = Tint;
	ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner());
	if (!Map || !Mesh || !Map->bHasBake)
	{
		if (GhostComponent)
		{
			GhostComponent->SetVisibility(false);
		}
		return;
	}
	if (!GhostComponent)
	{
		GhostComponent = NewObject<UStaticMeshComponent>(this);
		GhostComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GhostComponent->SetCastShadow(false);
		GhostComponent->SetupAttachment(Map->ContentRoot);
		GhostComponent->RegisterComponent();
		GhostMaterial = Map->MakeHoloMaterial(Tint, Map->HoloIntensity * 1.6f);
	}
	if (GhostComponent->GetStaticMesh() != Mesh)
	{
		GhostComponent->SetStaticMesh(Mesh);
		for (int32 MaterialIndex = 0; MaterialIndex < Mesh->GetStaticMaterials().Num(); ++MaterialIndex)
		{
			GhostComponent->SetMaterial(MaterialIndex, GhostMaterial);
		}
	}
	GhostMaterial->SetVectorParameterValue(TEXT("Color"), Tint);
	const float MeshScale = Map->BakedUnitsPerMetre() / SWGWorldScale * Map->EffectiveBuildingScale();
	const FVector Local = Map->RawToContent(FVector(Raw.X - Map->BakedCenter.X, Raw.Y - Map->BakedCenter.Y, Map->BakedHeightAt(Raw)));
	GhostComponent->SetRelativeTransform(FTransform(WorldRotation, Local, FVector(MeshScale, MeshScale, MeshScale * Map->EffectiveHeightExaggeration())));
	GhostComponent->SetVisibility(true);
}

void USWGHoloMapPlacementLayer::SetNoBuildAreas(const TArray<FNoBuildArea>& Areas)
{
	uint32 Hash = GetTypeHash(Areas.Num());
	for (const FNoBuildArea& Area : Areas)
	{
		Hash = HashCombine(Hash, GetTypeHash(FIntVector(FMath::RoundToInt(Area.Center.X * 10.f), FMath::RoundToInt(Area.Center.Y * 10.f),
			FMath::RoundToInt((Area.bCircle ? Area.Radius : Area.Rect.GetSize().X + Area.Rect.GetSize().Y) * 10.f) + (Area.bBlocking ? 1 : 0) + (Area.bPlacing ? 2 : 0))));
		Hash = HashCombine(Hash, GetTypeHash(Area.Tint.ToFColor(true).DWColor()));
	}
	if (Hash == NoBuildHash && Areas.Num() == NoBuildAreas.Num())
	{
		return;
	}
	NoBuildHash = Hash;
	NoBuildAreas = Areas;
	RebuildNoBuildAreas();
}

void USWGHoloMapPlacementLayer::RebuildNoBuildAreas()
{
	ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner());
	if (!Map || !Map->bHasBake)
	{
		return;
	}
	using namespace UE::Geometry;
	const auto EnsureComponent = [this, Map](TObjectPtr<UDynamicMeshComponent>& Component, const FLinearColor& Color, float Intensity)
	{
		if (Component)
		{
			return;
		}
		Component = NewObject<UDynamicMeshComponent>(this);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetupAttachment(Map->ContentRoot);
		Component->RegisterComponent();
		Component->SetMaterial(0, Map->MakeHoloMaterial(Color, Intensity));
	};
	// Dim amber for every reserved area, bright red for the ones stopping this placement.
	EnsureComponent(NoBuildComponent, FLinearColor(1.f, 0.5f, 0.1f), Map->HoloIntensity * 0.4f);
	EnsureComponent(BlockingComponent, FLinearColor(1.f, 0.1f, 0.08f), Map->HoloIntensity * 1.8f);
	EnsureComponent(PlacingComponent, FLinearColor::White, Map->HoloIntensity * 1.1f);

	const float Extent = Map->BakedRadius * Map->BakeExtentScale;
	// Just above the ground in content units, so the fill doesn't fight the terrain's depth.
	const FVector Lift(0.f, 0.f, 0.4f);
	FDynamicMesh3 Normal, Blocking, Placing;
	const auto AddCell = [Map, &Lift](FDynamicMesh3& Mesh, const FVector2D (&Corners)[4], float LiftScale)
	{
		int32 Vertices[4];
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const FVector Local = Map->RawToContent(FVector(Corners[Corner].X - Map->BakedCenter.X, Corners[Corner].Y - Map->BakedCenter.Y, Map->BakedHeightAt(Corners[Corner]))) + Lift * LiftScale;
			Vertices[Corner] = Mesh.AppendVertex(FVector3d(Local));
		}
		Mesh.AppendTriangle(Vertices[0], Vertices[2], Vertices[1]);
		Mesh.AppendTriangle(Vertices[0], Vertices[3], Vertices[2]);
	};
	for (const FNoBuildArea& Area : NoBuildAreas)
	{
		FDynamicMesh3& Mesh = Area.bPlacing ? Placing : Area.bBlocking ? Blocking : Normal;
		// The placing zone sits above the neighbours' so both stay readable where they overlap.
		const float LiftScale = Area.bPlacing ? 2.f : Area.bBlocking ? 1.4f : 1.f;
		if (Area.bPlacing)
		{
			if (UMaterialInstanceDynamic* PlacingMaterial = Cast<UMaterialInstanceDynamic>(PlacingComponent->GetMaterial(0))) { PlacingMaterial->SetVectorParameterValue(TEXT("Color"), Area.Tint); }
		}
		const FVector2D Middle = Area.bCircle ? Area.Center : Area.Rect.GetCenter();
		if (FVector2D::Distance(Middle, Map->BakedCenter) > Extent)
		{
			continue;
		}
		if (Area.bCircle)
		{
			const int32 Rings = FMath::Clamp(FMath::CeilToInt(Area.Radius / 6.f), 1, 12);
			constexpr int32 Segments = 40;
			for (int32 Ring = 0; Ring < Rings; ++Ring)
			{
				for (int32 Segment = 0; Segment < Segments; ++Segment)
				{
					const float InnerRadius = Area.Radius * Ring / Rings, OuterRadius = Area.Radius * (Ring + 1) / Rings;
					const float AngleA = 2.f * PI * Segment / Segments, AngleB = 2.f * PI * (Segment + 1) / Segments;
					const FVector2D Corners[4] = {
						Area.Center + FVector2D(FMath::Cos(AngleA), FMath::Sin(AngleA)) * InnerRadius,
						Area.Center + FVector2D(FMath::Cos(AngleA), FMath::Sin(AngleA)) * OuterRadius,
						Area.Center + FVector2D(FMath::Cos(AngleB), FMath::Sin(AngleB)) * OuterRadius,
						Area.Center + FVector2D(FMath::Cos(AngleB), FMath::Sin(AngleB)) * InnerRadius };
					AddCell(Mesh, Corners, LiftScale);
				}
			}
		}
		else
		{
			// Cells of about 4 m so the fill follows the ground's relief.
			const FVector2D Size = Area.Rect.GetSize();
			const int32 CellsX = FMath::Clamp(FMath::CeilToInt(Size.X / 4.f), 1, 16), CellsY = FMath::Clamp(FMath::CeilToInt(Size.Y / 4.f), 1, 16);
			for (int32 CellY = 0; CellY < CellsY; ++CellY)
			{
				for (int32 CellX = 0; CellX < CellsX; ++CellX)
				{
					const FVector2D Min = Area.Rect.Min + FVector2D(Size.X * CellX / CellsX, Size.Y * CellY / CellsY);
					const FVector2D Max = Area.Rect.Min + FVector2D(Size.X * (CellX + 1) / CellsX, Size.Y * (CellY + 1) / CellsY);
					const FVector2D Corners[4] = { Min, FVector2D(Max.X, Min.Y), Max, FVector2D(Min.X, Max.Y) };
					AddCell(Mesh, Corners, LiftScale);
				}
			}
		}
	}
	NoBuildComponent->SetMesh(MoveTemp(Normal));
	BlockingComponent->SetMesh(MoveTemp(Blocking));
	PlacingComponent->SetMesh(MoveTemp(Placing));
}
