#include "Objects/World/SWGStructurePlacementPreview.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Structure/SWGPlacementRules.h"
#include "Common/SWGWorldScale.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstance.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"

ASWGStructurePlacementPreview::ASWGStructurePlacementPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Ghost = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ghost"));
	Ghost->SetupAttachment(Root);
	Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ghost->SetCastShadow(false);
	Printed = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Printed"));
	Printed->SetupAttachment(Root);
	Printed->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Printed->SetVisibility(false);
	BodyGrid = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("BodyGrid"));
	BodyGrid->SetupAttachment(Root);
	BodyGrid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyGrid->SetCastShadow(false);
	ReservedGrid = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("ReservedGrid"));
	ReservedGrid->SetupAttachment(Root);
	ReservedGrid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ReservedGrid->SetCastShadow(false);
}

void ASWGStructurePlacementPreview::Initialize(const FString& TemplatePath, USWGMeshGeneratorSubsystem* Meshes,
	USWGTerrainSubsystem* InTerrain, USWGObjectGraphSubsystem* InObjects, const USWGStructurePlacementSubsystem* InPlacement)
{
	Placement = InPlacement;
	Terrain = InTerrain;
	Objects = InObjects;
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGHologram.M_SWGHologram"));
	const auto MakeMaterial = [this, Parent](float Intensity)
	{
		UMaterialInstanceDynamic* MID = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
		if (MID)
		{
			MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
			MID->SetScalarParameterValue(TEXT("Radius"), 1000000.f);
			MID->SetScalarParameterValue(TEXT("Fade"), 1.f);
		}
		return MID;
	};
	GhostMaterial = MakeMaterial(.5f);
	BodyMaterial = MakeMaterial(.6f);
	ReservedMaterial = MakeMaterial(.25f);
	BodyGrid->SetMaterial(0, BodyMaterial);
	ReservedGrid->SetMaterial(0, ReservedMaterial);
	if (Meshes && !TemplatePath.IsEmpty())
	{
		TWeakObjectPtr<ASWGStructurePlacementPreview> WeakThis(this);
		Meshes->RequestTemplateStaticMesh(TemplatePath, [WeakThis](UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
		{
			if (!WeakThis.IsValid() || !Mesh) { return; }
			WeakThis->Ghost->SetStaticMesh(Mesh);
			WeakThis->Printed->SetStaticMesh(Mesh);
			WeakThis->SourceMaterials.Reset();
			for (int32 Index = 0; Index < Mesh->GetStaticMaterials().Num(); ++Index)
			{
				WeakThis->SourceMaterials.Add(Materials.IsValidIndex(Index) ? Materials[Index] : Mesh->GetMaterial(Index));
				WeakThis->Ghost->SetMaterial(Index, WeakThis->GhostMaterial);
			}
			if (WeakThis->bPrinting) { WeakThis->SetupConstructionMaterials(); WeakThis->SetConstructionProgress(WeakThis->ConstructionProgress); }
		});
	}
}

void ASWGStructurePlacementPreview::UpdatePlacement(const FVector2D& RawCenter, int32 Rotation,
	const FSWGStructureFootprint& Footprint, float ClearFloraRadius, bool bSnapToTerrain, ESWGPlacementVerdict Verdict)
{
	if (!Terrain) { return; }
	Center = RawCenter;
	Angle = Rotation * 90;
	CurrentFootprint = Footprint;
	CurrentVerdict = Verdict;
	const FLinearColor Color = bHologramOnly ? FLinearColor(.12f, .55f, 1.f)
		: Verdict == ESWGPlacementVerdict::Invalid ? FLinearColor(1.f, .12f, .08f)
		: Verdict == ESWGPlacementVerdict::Uncertain ? FLinearColor(1.f, .6f, .07f) : FLinearColor(.15f, 1.f, .3f);
	for (UMaterialInstanceDynamic* Material : { GhostMaterial.Get(), BodyMaterial.Get(), ReservedMaterial.Get() })
	{
		if (Material) { Material->SetVectorParameterValue(TEXT("Color"), Color); }
	}
	float BaseHeight = Terrain->GetHeightAt(Center.X, Center.Y);
	if (ClearFloraRadius > 0.f && !bSnapToTerrain && Footprint.IsValid())
	{
		for (int32 Row = 0; Row < Footprint.Rows; ++Row)
		{
			for (int32 Col = 0; Col < Footprint.Cols; ++Col)
			{
				if (Footprint.GetCell(Col, Row) != ESWGFootprintCell::Structure) { continue; }
				const FVector2D P((Col - Footprint.CenterCol) * Footprint.ColChunkSize,
					(Row - Footprint.CenterRow) * Footprint.RowChunkSize);
				const float R = FMath::DegreesToRadians(static_cast<float>(Angle));
				BaseHeight = FMath::Max(BaseHeight, Terrain->GetHeightAt(Center.X + P.X * FMath::Cos(R) + P.Y * FMath::Sin(R),
					Center.Y - P.X * FMath::Sin(R) + P.Y * FMath::Cos(R)));
			}
		}
	}
	Ghost->SetWorldLocation(SWGToUnrealSpace(FVector(Center.X, Center.Y, BaseHeight)));
	const float HalfAngle = FMath::DegreesToRadians(static_cast<float>(Angle)) * .5f;
	Ghost->SetWorldRotation(SWGNativeToUnrealRotation(0.f, FMath::Sin(HalfAngle), 0.f, FMath::Cos(HalfAngle)));
	Printed->SetWorldTransform(Ghost->GetComponentTransform());
	UE::Geometry::FDynamicMesh3 Body, Reserved;
	const float R = FMath::DegreesToRadians(static_cast<float>(Angle));
	const float C = FMath::Cos(R), S = FMath::Sin(R);
	for (int32 Row = 0; Row < Footprint.Rows; ++Row)
	{
		for (int32 Col = 0; Col < Footprint.Cols; ++Col)
		{
			const ESWGFootprintCell Cell = Footprint.GetCell(Col, Row);
			if (Cell == ESWGFootprintCell::Outside) { continue; }
			UE::Geometry::FDynamicMesh3& Mesh = Cell == ESWGFootprintCell::Structure ? Body : Reserved;
			const float X = (Col - Footprint.CenterCol) * Footprint.ColChunkSize;
			const float Y = (Row - Footprint.CenterRow) * Footprint.RowChunkSize;
			const float HX = FMath::Max(.01f, Footprint.ColChunkSize * .5f - .06f);
			const float HY = FMath::Max(.01f, Footprint.RowChunkSize * .5f - .06f);
			const FVector2D Corners[] = {{X-HX,Y-HY}, {X+HX,Y-HY}, {X+HX,Y+HY}, {X-HX,Y+HY}};
			int32 V[4];
			for (int32 I = 0; I < 4; ++I)
			{
				const FVector2D World(Center.X + Corners[I].X * C + Corners[I].Y * S,
					Center.Y - Corners[I].X * S + Corners[I].Y * C);
				V[I] = Mesh.AppendVertex(SWGToUnrealSpace(FVector(World.X, World.Y, Terrain->GetHeightAt(World.X, World.Y) + .15f)));
			}
			Mesh.AppendTriangle(V[0], V[2], V[1]);
			Mesh.AppendTriangle(V[0], V[3], V[2]);
		}
	}
	BodyGrid->SetMesh(MoveTemp(Body));
	ReservedGrid->SetMesh(MoveTemp(Reserved));
}

void ASWGStructurePlacementPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPresentationHidden || bHologramOnly) { return; }
	if (!Terrain || !Objects) { return; }
	const AActor* Player = Objects->FindActor(Objects->GetLocalPlayerObjectId());
	if (!Player && GetWorld() && GetWorld()->GetFirstPlayerController()) { Player = GetWorld()->GetFirstPlayerController()->GetPawn(); }
	if (!Player && !bMarkerOnly) { return; }
	const FVector P = Player ? SWGToRawSpace(Player->GetActorLocation()) : FVector::ZeroVector;
	const FVector2D PlayerRaw(P.X, P.Y);
	const FColor Color = bMarkerOnly ? FColor(255, 204, 26) : CurrentVerdict == ESWGPlacementVerdict::Invalid ? FColor::Red
		: CurrentVerdict == ESWGPlacementVerdict::Uncertain ? FColor::Orange : FColor::Green;
	const auto Point = [this](const FVector2D& Raw)
	{
		return SWGToUnrealSpace(FVector(Raw.X, Raw.Y, Terrain->GetHeightAt(Raw.X, Raw.Y) + .2f));
	};
	for (int32 I = 0; I < (bMarkerOnly ? 0 : 64); ++I)
	{
		const float A = 2.f * PI * I / 64.f, B = 2.f * PI * (I + 1) / 64.f;
		DrawDebugLine(GetWorld(), Point(PlayerRaw + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 100.f),
			Point(PlayerRaw + FVector2D(FMath::Cos(B), FMath::Sin(B)) * 100.f), Color, false, 0.f, 0, 2.f);
	}
	// Rects follow the terrain: edges are split into ~4 m segments so they aren't buried by slopes.
	const auto DrawRect = [this, &Point](const FBox2D& Rect, const FColor& RectColor, float Thickness)
	{
		const FVector2D Corners[] = {Rect.Min, {Rect.Max.X, Rect.Min.Y}, Rect.Max, {Rect.Min.X, Rect.Max.Y}};
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			const FVector2D From = Corners[Edge], To = Corners[(Edge + 1) % 4];
			const int32 Segments = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(From, To) / 4.f));
			for (int32 Segment = 0; Segment < Segments; ++Segment)
			{
				DrawDebugLine(GetWorld(), Point(FMath::Lerp(From, To, static_cast<float>(Segment) / Segments)),
					Point(FMath::Lerp(From, To, static_cast<float>(Segment + 1) / Segments)), RectColor, false, 0.f, 0, Thickness);
			}
		}
	};
	// The server's reserved rect for this placement, then every nearby structure's; the blockers are the red ones.
	// A marker is just the filled footprint area, no border line.
	if (CurrentFootprint.IsValid() && !bMarkerOnly)
	{
		const FBox2D Local = FSWGPlacementRules::GetServerFootprintRect(CurrentFootprint, Angle);
		DrawRect(FBox2D(Local.Min + Center, Local.Max + Center), Color, 14.f);
	}
	if (const USWGStructurePlacementSubsystem* Subsystem = bMarkerOnly ? nullptr : Placement.Get())
	{
		for (const FSWGPlacementNeighbour& Neighbour : Subsystem->GetNeighbours())
		{
			if (!Neighbour.bHasRect || Neighbour.DistanceSq > FMath::Square(160.f)) { continue; }
			DrawRect(Neighbour.Rect, Neighbour.bBlocking ? FColor::Red : FColor(150, 170, 200), Neighbour.bBlocking ? 22.f : 8.f);
		}
	}
}

void ASWGStructurePlacementPreview::ShowAsMarker()
{
	bMarkerOnly = true;
	bHologramOnly = false;
	BodyGrid->SetVisibility(true);
	ReservedGrid->SetVisibility(true);
	Ghost->SetVisibility(false);
	// Retail styles the construction-site footprint with its yellow placement shader.
	const FLinearColor Color(1.f, .8f, .1f);
	for (UMaterialInstanceDynamic* Material : { BodyMaterial.Get(), ReservedMaterial.Get() })
	{
		if (Material) { Material->SetVectorParameterValue(TEXT("Color"), Color); }
	}
}

void ASWGStructurePlacementPreview::SetPresentationHidden(bool bHide)
{
	bPresentationHidden = bHide;
	SetActorHiddenInGame(bHide);
}

void ASWGStructurePlacementPreview::SetHologramOnly(bool bEnable)
{
	bHologramOnly = bEnable;
	BodyGrid->SetVisibility(!bEnable);
	ReservedGrid->SetVisibility(!bEnable);
	const FLinearColor Color = bEnable ? FLinearColor(.12f, .55f, 1.f)
		: CurrentVerdict == ESWGPlacementVerdict::Invalid ? FLinearColor(1.f, .12f, .08f)
		: CurrentVerdict == ESWGPlacementVerdict::Uncertain ? FLinearColor(1.f, .6f, .07f) : FLinearColor(.15f, 1.f, .3f);
	if (GhostMaterial) { GhostMaterial->SetVectorParameterValue(TEXT("Color"), Color); }
}

void ASWGStructurePlacementPreview::BeginConstructionPrint()
{
	bPrinting = true;
	bHologramOnly = true;
	BodyGrid->SetVisibility(false);
	ReservedGrid->SetVisibility(false);
	SetupConstructionMaterials();
	SetConstructionProgress(0.f);
}

void ASWGStructurePlacementPreview::SetupConstructionMaterials()
{
	if (!Ghost->GetStaticMesh() || ConstructionHoloMaterial) { return; }
	UMaterialInterface* HoloParent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGConstructionHolo.M_SWGConstructionHolo"));
	if (!HoloParent) { return; }
	ConstructionHoloMaterial = UMaterialInstanceDynamic::Create(HoloParent, this);
	ConstructionHoloMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(.12f, .55f, 1.f));
	ConstructionHoloMaterial->SetScalarParameterValue(TEXT("Intensity"), .5f);
	ConstructionHoloMaterial->SetScalarParameterValue(TEXT("Radius"), 1000000.f);
	ConstructionHoloMaterial->SetScalarParameterValue(TEXT("Fade"), 1.f);
	for (int32 Index = 0; Index < Ghost->GetNumMaterials(); ++Index) { Ghost->SetMaterial(Index, ConstructionHoloMaterial); }

	UMaterialInterface* Opaque = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGConstructionTextured.M_SWGConstructionTextured"));
	UMaterialInterface* Masked = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGConstructionTexturedMasked.M_SWGConstructionTexturedMasked"));
	UMaterialInterface* Translucent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGConstructionTranslucent.M_SWGConstructionTranslucent"));
	PrintedMaterials.Reset();
	for (int32 Index = 0; Index < SourceMaterials.Num(); ++Index)
	{
		UMaterialInterface* Source = SourceMaterials[Index];
		UMaterialInterface* Parent = Source && Source->GetBlendMode() == BLEND_Translucent ? Translucent
			: Source && Source->GetBlendMode() == BLEND_Masked ? Masked : Opaque;
		UMaterialInstanceDynamic* Material = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
		if (Material)
		{
			if (UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(Source)) { Material->CopyParameterOverrides(SourceInstance); }
			Printed->SetMaterial(Index, Material);
		}
		PrintedMaterials.Add(Material);
	}
	Printed->SetVisibility(true);
}

void ASWGStructurePlacementPreview::SetConstructionProgress(float Progress)
{
	ConstructionProgress = FMath::Clamp(Progress, 0.f, 1.f);
	if (!Ghost->GetStaticMesh()) { return; }
	const FBoxSphereBounds Bounds = Ghost->Bounds;
	const float Bottom = FMath::Max(Ghost->GetComponentLocation().Z, Bounds.Origin.Z - Bounds.BoxExtent.Z) - 10.f;
	const float Top = Bounds.Origin.Z + Bounds.BoxExtent.Z + 10.f;
	const float RevealZ = FMath::Lerp(Bottom, Top, ConstructionProgress);
	if (ConstructionHoloMaterial) { ConstructionHoloMaterial->SetScalarParameterValue(TEXT("RevealZ"), RevealZ); }
	for (UMaterialInstanceDynamic* Material : PrintedMaterials)
	{
		if (Material) { Material->SetScalarParameterValue(TEXT("RevealZ"), RevealZ); }
	}
}
