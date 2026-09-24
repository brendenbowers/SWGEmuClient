#include "SWGHoloMapActor.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Common/SWGWorldScale.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "TRE/SWGCrc32.h"
#include "Components/TextRenderComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Objects/SWGObject.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGInstallation.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "TRE/SWGTerrainEvaluator.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGWorldSnapshotReader.h"

namespace
{
	constexpr int32 GridSize = 121;
	/** The bake covers this much more than the radius, so panning and zooming out show ground before the rebake lands. */
	constexpr float BakeExtentScale = 1.5f;
	/** Rebake once the view drifts this far (fraction of the radius) or zooms past this ratio. */
	constexpr float RebakeDrift = 0.35f;
	constexpr float RebakeZoomRatio = 1.25f;
	/** Seconds the view must hold still before a rebake starts. */
	constexpr double RebakeSettleSeconds = 0.25;
	constexpr int32 MaxBuildings = 150;
	/** How far above the projector the content floats, world units. */
	constexpr float ContentLift = 12.f;
	constexpr float FadeInSeconds = 0.5f;
	constexpr float LabelWorldSize = 4.f;
	/** Crowded marker labels rise in steps of about one text line, world units. */
	constexpr float LabelTierStep = LabelWorldSize * 1.25f;
	constexpr int32 MaxLabelTiers = 6;
	/** The player's marker glows brighter than the map and pulses at this period. */
	constexpr float PlayerMarkerGlow = 4.f;
	constexpr float PlayerPulseSeconds = 1.6f;
	const FName HoloPlayerStyle(TEXT("Player"));

	/** Raw offset (x east, y north, z up, metres) to UE axes, unscaled. */
	FVector RawAxes(const FVector& Raw)
	{
		return FVector(Raw.Y, Raw.X, Raw.Z);
	}

	ASWGHoloMapActor::FTerrainBake BakeTerrainPatch(TSharedPtr<const FSWGTerrainData, ESPMode::ThreadSafe> Planet,
		TSharedPtr<const TArray<FSWGTerrainLayer>, ESPMode::ThreadSafe> Edits, FVector2D Center, float Extent,
		float UnitsPerMetre, float Exaggeration)
	{
		using namespace UE::Geometry;
		ASWGHoloMapActor::FTerrainBake Bake;
		if (!Planet)
		{
			return Bake;
		}
		const TArrayView<const FSWGTerrainLayer> EditView = Edits ? MakeArrayView(*Edits) : TArrayView<const FSWGTerrainLayer>();
		const float Spacing = 2.f * Extent / (GridSize - 1);
		Bake.Heights.SetNumUninitialized(GridSize * GridSize);
		ParallelFor(GridSize, [&](int32 Row)
		{
			for (int32 Column = 0; Column < GridSize; ++Column)
			{
				Bake.Heights[Row * GridSize + Column] = FSWGTerrainEvaluator::GetHeight(*Planet,
					Center.X - Extent + Column * Spacing, Center.Y - Extent + Row * Spacing, EditView);
			}
		});
		Bake.BaseHeight = FMath::Min(Bake.Heights);
		if (Planet->Header.bUseGlobalWaterTable)
		{
			// The sea reads as a flat plane, as it does in game.
			Bake.BaseHeight = FMath::Max(Bake.BaseHeight, Planet->Header.GlobalWaterTableHeight);
			for (float& Height : Bake.Heights)
			{
				Height = FMath::Max(Height, Planet->Header.GlobalWaterTableHeight);
			}
		}

		Bake.Mesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
		FDynamicMesh3& Mesh = *Bake.Mesh;
		Mesh.EnableAttributes();
		FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		auto HeightAt = [&Bake](int32 Column, int32 Row)
		{
			return Bake.Heights[FMath::Clamp(Row, 0, GridSize - 1) * GridSize + FMath::Clamp(Column, 0, GridSize - 1)];
		};
		const float VerticalScale = UnitsPerMetre * Exaggeration;
		for (int32 Row = 0; Row < GridSize; ++Row)
		{
			for (int32 Column = 0; Column < GridSize; ++Column)
			{
				const FVector RawOffset(-Extent + Column * Spacing, -Extent + Row * Spacing, 0.f);
				FVector Local = RawAxes(RawOffset) * UnitsPerMetre;
				Local.Z = (HeightAt(Column, Row) - Bake.BaseHeight) * VerticalScale;
				Mesh.AppendVertex(FVector3d(Local));
				// Normals in the exaggerated space, so slopes glow as steep as they look.
				const float SlopeEast = (HeightAt(Column + 1, Row) - HeightAt(Column - 1, Row)) * Exaggeration / (2.f * Spacing);
				const float SlopeNorth = (HeightAt(Column, Row + 1) - HeightAt(Column, Row - 1)) * Exaggeration / (2.f * Spacing);
				Normals->AppendElement(FVector3f(RawAxes(FVector(-SlopeEast, -SlopeNorth, 1.f)).GetSafeNormal()));
			}
		}
		const float KeepRadius = Extent;
		for (int32 Row = 0; Row < GridSize - 1; ++Row)
		{
			for (int32 Column = 0; Column < GridSize - 1; ++Column)
			{
				// A round patch: the square's corners would only ever be faded out.
				const FVector2D CellCentre(-Extent + (Column + 0.5f) * Spacing, -Extent + (Row + 0.5f) * Spacing);
				if (CellCentre.Size() > KeepRadius)
				{
					continue;
				}
				const int32 SouthWest = Row * GridSize + Column;
				const int32 SouthEast = SouthWest + 1;
				const int32 NorthWest = SouthWest + GridSize;
				const int32 NorthEast = NorthWest + 1;
				// Rows step north (UE +X) like the streamed tiles, so the same winding faces up.
				for (const FIndex3i& Triangle : { FIndex3i(SouthWest, SouthEast, NorthWest), FIndex3i(SouthEast, NorthEast, NorthWest) })
				{
					const int32 TriangleId = Mesh.AppendTriangle(Triangle);
					if (TriangleId >= 0)
					{
						Normals->SetTriangle(TriangleId, Triangle);
					}
				}
			}
		}
		return Bake;
	}
}

ASWGHoloMapActor::ASWGHoloMapActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Projector")));

	ContentRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Content"));
	ContentRoot->SetupAttachment(GetRootComponent());

	TerrainComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Terrain"));
	TerrainComponent->SetupAttachment(ContentRoot);
	TerrainComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TerrainComponent->SetCastShadow(false);

	BaseComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Base"));
	BaseComponent->SetupAttachment(GetRootComponent());
	BaseComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BaseComponent->SetCastShadow(false);

	DroidRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Droid"));
	DroidRoot->SetupAttachment(GetRootComponent());

	DroidLens = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DroidLens"));
	DroidLens->SetupAttachment(DroidRoot);
	DroidLens->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DroidLens->SetCastShadow(false);
	DroidLens->SetRelativeLocation(FVector(0.f, 0.f, -8.f));
	DroidLens->SetRelativeScale3D(FVector(0.04f));

	DroidLensLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("DroidLensLight"));
	DroidLensLight->SetupAttachment(DroidLens);
	DroidLensLight->SetCastShadows(false);
	DroidLensLight->SetIntensityUnits(ELightUnits::Candelas);
	DroidLensLight->SetIntensity(4.f);
	DroidLensLight->SetLightColor(FLinearColor(0.12f, 0.55f, 1.f));
	DroidLensLight->SetAttenuationRadius(55.f);

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	GlowLight->SetupAttachment(GetRootComponent());
	GlowLight->SetCastShadows(false);
	GlowLight->SetIntensityUnits(ELightUnits::Candelas);
	// A faint blue spill on whatever is near, not a lamp.
	GlowLight->SetIntensity(1.5f);
	GlowLight->SetLightColor(FLinearColor(0.2f, 0.55f, 1.f));
	GlowLight->SetAttenuationRadius(250.f);
	GlowLight->SetRelativeLocation(FVector(0.f, 0.f, 30.f));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HoloFinder(TEXT("/Game/SWGEmu/Materials/M_SWGHologram.M_SWGHologram"));
	HoloMaterial = HoloFinder.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	BeamMesh = CylinderFinder.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	ArrowMesh = ConeFinder.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	BaseComponent->SetStaticMesh(BeamMesh);
	DroidLens->SetStaticMesh(SphereFinder.Object);
}

UMaterialInstanceDynamic* ASWGHoloMapActor::MakeHoloMaterial(const FLinearColor& Color, float Intensity)
{
	UMaterialInstanceDynamic* Material = HoloMaterial ? UMaterialInstanceDynamic::Create(HoloMaterial, this) : nullptr;
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		// A little past the disc so pins standing on the rim aren't cut.
		Material->SetScalarParameterValue(TEXT("Radius"), DiscDiameter * 0.52f);
		Material->SetScalarParameterValue(TEXT("Fade"), FadeAlpha);
		HoloMaterials.Add(Material);
	}
	return Material;
}

void ASWGHoloMapActor::BeginPlay()
{
	Super::BeginPlay();
	ContentMaterial = MakeHoloMaterial(HoloColor, HoloIntensity);
	TerrainComponent->SetMaterial(0, ContentMaterial);
	ContentRoot->SetRelativeLocation(FVector(0.f, 0.f, ContentLift));

	// The projector: a thin glowing disc a touch wider than the image.
	const float BaseScale = DiscDiameter * 1.05f / 100.f;
	BaseComponent->SetRelativeScale3D(FVector(BaseScale, BaseScale, 0.03f));
	// A faint light field under the image; the droid above is the projector now.
	BaseComponent->SetMaterial(0, MakeHoloMaterial(HoloColor, HoloIntensity * 0.12f));
	DroidLens->SetMaterial(0, MakeHoloMaterial(HoloColor, HoloIntensity * 2.f));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UStaticMeshComponent* Line = NewObject<UStaticMeshComponent>(this);
		Line->SetStaticMesh(BeamMesh);
		Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Line->SetCastShadow(false);
		Line->SetMaterial(0, MakeHoloMaterial(HoloColor, HoloIntensity * 0.12f));
		Line->SetupAttachment(GetRootComponent());
		Line->RegisterComponent();
		ProjectionLines.Add(Line);
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UStaticMeshComponent* Line = NewObject<UStaticMeshComponent>(this);
		Line->SetStaticMesh(BeamMesh);
		Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Line->SetCastShadow(false);
		Line->SetMaterial(0, MakeHoloMaterial(HoloColor, 0.f));
		Line->SetupAttachment(GetRootComponent());
		Line->RegisterComponent();
		SweepLines.Add(Line);
	}
	LastViewChangeTime = FPlatformTime::Seconds();
	RequestDroid();
	UpdateDroid();
}

void ASWGHoloMapActor::SetDroidSide(const FVector2D& Direction)
{
	DroidSide = Direction.GetSafeNormal();
	if (DroidSide.IsNearlyZero())
	{
		DroidSide = FVector2D(1.f, 0.f);
	}
	UpdateDroid();
}

void ASWGHoloMapActor::RequestDroid()
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	if (!MeshGenerator || DroidTemplate.IsEmpty())
	{
		return;
	}
	TWeakObjectPtr<ASWGHoloMapActor> WeakThis(this);
	auto Attach = [WeakThis](UMeshComponent* Mesh, const TArray<UMaterialInterface*>& Materials)
	{
		ASWGHoloMapActor* Actor = WeakThis.Get();
		if (!Actor)
		{
			return;
		}
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetupAttachment(Actor->DroidRoot);
		Mesh->SetRelativeScale3D(FVector(Actor->DroidScale));
		for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
		{
			Mesh->SetMaterial(MaterialIndex, Materials[MaterialIndex]);
		}
		Mesh->RegisterComponent();
	};
	// A mobile template: skeletal when its appearance is a .sat, shown in bind pose.
	MeshGenerator->RequestItemMesh(FSWGCrc32::HashString(DroidTemplate), INDEX_NONE, FSWGCustomizationVariables(),
		[WeakThis, Attach](UStaticMesh* Mesh, const FSWGMeshData, const TArray<UMaterialInterface*>& Materials)
		{
			ASWGHoloMapActor* Actor = WeakThis.Get();
			if (Actor && Mesh)
			{
				UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Actor);
				Component->SetStaticMesh(Mesh);
				Attach(Component, Materials);
			}
		},
		[WeakThis, Attach](USkeletalMesh* Mesh, const FSWGMeshData, const TArray<UMaterialInterface*>& Materials)
		{
			ASWGHoloMapActor* Actor = WeakThis.Get();
			if (Actor && Mesh)
			{
				USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Actor);
				Component->SetSkeletalMeshAsset(Mesh);
				Attach(Component, Materials);
			}
		});
}

void ASWGHoloMapActor::UpdateDroid()
{
	const float DiscRadius = DiscDiameter * 0.5f;
	// A slow bob and a slight sway, so it reads as hovering rather than fixed.
	const float Bob = FMath::Sin(DroidTime * 1.6f) * 3.f;
	const FVector Hover = FVector(DroidSide.X, DroidSide.Y, 0.f) * DiscRadius * DroidReach
		+ FVector(0.f, 0.f, ContentLift + DiscRadius * DroidHeight + Bob);
	const FVector ToCentre = FVector(0.f, 0.f, ContentLift) - Hover;
	DroidRoot->SetRelativeLocation(Hover);
	DroidRoot->SetRelativeRotation(FRotator(0.f, ToCentre.Rotation().Yaw + ViewYaw + FMath::Sin(DroidTime * 0.7f) * 8.f, 0.f));

	const FVector Origin = Hover + FVector(0.f, 0.f, -8.f);
	auto PlaceRay = [this, &Origin, DiscRadius](UStaticMeshComponent* Line, float Angle)
	{
		const FVector2D RimDirection = DroidSide.GetRotated(Angle + ProjectionYawOffset);
		const FVector Rim(RimDirection.X * DiscRadius * 0.96f,
			RimDirection.Y * DiscRadius * 0.96f, ContentLift);
		const FVector Ray = Rim - Origin;
		Line->SetRelativeLocation((Origin + Rim) * 0.5f);
		Line->SetRelativeRotation(FRotationMatrix::MakeFromZ(Ray).Rotator());
		Line->SetRelativeScale3D(FVector(0.007f, 0.007f, Ray.Size() / 100.f));
	};
	for (int32 Index = 0; Index < ProjectionLines.Num(); ++Index)
	{
		// Span the far half of the rim, leaving the two near-side rays out of the player's view.
		PlaceRay(ProjectionLines[Index], -90.f + 60.f * Index);
	}
	const float IdleSeconds = FPlatformTime::Seconds() - LastProjectionChangeTime;
	const float SweepFade = FMath::Clamp((0.65f - IdleSeconds) / 0.4f, 0.f, 1.f);
	for (int32 Index = 0; Index < SweepLines.Num(); ++Index)
	{
		UStaticMeshComponent* Line = SweepLines[Index];
		const float Phase = FMath::Frac((DroidTime - SweepStartTime) * 1.2f);
		PlaceRay(Line, Index == 0 ? -90.f + 180.f * Phase : 90.f - 180.f * Phase);
		Line->SetVisibility(SweepFade > 0.01f);
		if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Line->GetMaterial(0)))
		{
			Material->SetScalarParameterValue(TEXT("Intensity"), HoloIntensity * 0.16f * SweepFade);
		}
	}
}

void ASWGHoloMapActor::NoteProjectionChange()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastProjectionChangeTime > 0.65)
	{
		SweepStartTime = DroidTime;
	}
	LastProjectionChangeTime = Now;
}

void ASWGHoloMapActor::SetViewCenter(const FVector2D& RawCenter)
{
	if (RawCenter.Equals(ViewCenter))
	{
		return;
	}
	ViewCenter = RawCenter;
	LastViewChangeTime = FPlatformTime::Seconds();
	NoteProjectionChange();
	if (!bHasBake)
	{
		bBakeWanted = true;
	}
	UpdateContentTransform();
}

void ASWGHoloMapActor::SetViewRadius(float RawRadius)
{
	const float NewRadius = FMath::Clamp(RawRadius, MinRadius, MaxRadius);
	if (FMath::IsNearlyEqual(NewRadius, ViewRadius))
	{
		return;
	}
	ViewRadius = NewRadius;
	LastViewChangeTime = FPlatformTime::Seconds();
	NoteProjectionChange();
	UpdateContentTransform();
}

void ASWGHoloMapActor::SetViewYaw(float Degrees)
{
	const float NewYaw = FRotator::NormalizeAxis(Degrees);
	if (FMath::IsNearlyEqual(NewYaw, ViewYaw))
	{
		return;
	}
	ProjectionYawOffset = FRotator::NormalizeAxis(ProjectionYawOffset + FMath::FindDeltaAngleDegrees(ViewYaw, NewYaw));
	ViewYaw = NewYaw;
	NoteProjectionChange();
	UpdateContentTransform();
}

FVector ASWGHoloMapActor::RawToContent(const FVector& RawOffset) const
{
	FVector Local = RawAxes(FVector(RawOffset.X, RawOffset.Y, 0.f)) * BakedUnitsPerMetre();
	Local.Z = (RawOffset.Z - BakedBaseHeight) * BakedUnitsPerMetre() * HeightExaggeration;
	return Local;
}

float ASWGHoloMapActor::BakedHeightAt(const FVector2D& Raw) const
{
	if (BakedHeights.Num() != GridSize * GridSize)
	{
		return BakedBaseHeight;
	}
	const float Extent = BakedRadius * BakeExtentScale;
	const float Spacing = 2.f * Extent / (GridSize - 1);
	const float Column = FMath::Clamp((Raw.X - BakedCenter.X + Extent) / Spacing, 0.f, GridSize - 1.f);
	const float Row = FMath::Clamp((Raw.Y - BakedCenter.Y + Extent) / Spacing, 0.f, GridSize - 1.f);
	const int32 Column0 = FMath::Min(FMath::FloorToInt(Column), GridSize - 2);
	const int32 Row0 = FMath::Min(FMath::FloorToInt(Row), GridSize - 2);
	const float South = FMath::Lerp(BakedHeights[Row0 * GridSize + Column0], BakedHeights[Row0 * GridSize + Column0 + 1], Column - Column0);
	const float North = FMath::Lerp(BakedHeights[(Row0 + 1) * GridSize + Column0], BakedHeights[(Row0 + 1) * GridSize + Column0 + 1], Column - Column0);
	return FMath::Lerp(South, North, Row - Row0);
}

void ASWGHoloMapActor::UpdateContentTransform()
{
	// Content is laid out around BakedCenter at BakedRadius; scale it to the live
	// radius, turn it, and slide it so ViewCenter lands on the disc's centre.
	const float Scale = BakedRadius / ViewRadius;
	const FRotator Yaw(0.f, ViewYaw, 0.f);
	const FVector ViewInContent = RawToContent(FVector(ViewCenter - BakedCenter, BakedBaseHeight));
	const FVector Offset = Yaw.RotateVector(-FVector(ViewInContent.X, ViewInContent.Y, 0.f) * Scale);
	ContentRoot->SetRelativeTransform(FTransform(Yaw, Offset + FVector(0.f, 0.f, ContentLift), FVector(Scale)));
	if (ContentMaterial)
	{
		// Contours stay the same number of metres apart whatever the zoom.
		ContentMaterial->SetScalarParameterValue(TEXT("ContourSpacing"), ContourMetres * BakedUnitsPerMetre() * HeightExaggeration * Scale);
	}
}

void ASWGHoloMapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FadeAlpha = FMath::Min(1.f, FadeAlpha + DeltaSeconds / FadeInSeconds);
	DroidTime += DeltaSeconds;
	if (FPlatformTime::Seconds() - LastProjectionChangeTime > 0.25)
	{
		ProjectionYawOffset = FMath::FInterpTo(ProjectionYawOffset, 0.f, DeltaSeconds, 3.f);
	}
	UpdateDroid();
	// The material fades everything past the rim, measured from here.
	const FVector Centre = GetActorLocation();
	HoloMaterials.RemoveAll([](const TObjectPtr<UMaterialInstanceDynamic>& Material) { return !Material; });
	for (UMaterialInstanceDynamic* Material : HoloMaterials)
	{
		Material->SetVectorParameterValue(TEXT("Center"), FLinearColor(Centre.X, Centre.Y, Centre.Z, 0.f));
		Material->SetScalarParameterValue(TEXT("Fade"), FadeAlpha);
	}

	const bool bDrifted = bHasBake && (FVector2D::Distance(ViewCenter, BakedCenter) > BakedRadius * RebakeDrift
		|| FMath::Max(ViewRadius / BakedRadius, BakedRadius / ViewRadius) > RebakeZoomRatio);
	const bool bSettled = FPlatformTime::Seconds() - LastViewChangeTime > RebakeSettleSeconds;
	if ((bBakeWanted || bDrifted) && bSettled && !bBakeInFlight)
	{
		RequestBake();
	}
	if (bHasBake && FPlatformTime::Seconds() >= NextDynamicScanTime)
	{
		RefreshDynamicStructures(/*bRebuild=*/false);
	}
	UpdateMarkers();
}

void ASWGHoloMapActor::RequestBake()
{
	const UGameInstance* GameInstance = GetGameInstance();
	const USWGTerrainSubsystem* Terrain = GameInstance ? GameInstance->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
	if (!Terrain || !Terrain->GetPlanetData())
	{
		return;
	}
	bBakeWanted = false;
	bBakeInFlight = true;
	const int32 Generation = ++BakeGeneration;
	const FVector2D Center = ViewCenter;
	const float Radius = ViewRadius;
	const float UnitsPerMetre = DiscDiameter * 0.5f / Radius;
	TWeakObjectPtr<ASWGHoloMapActor> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, Generation, Center, Radius, UnitsPerMetre, Exaggeration = HeightExaggeration,
		Planet = Terrain->GetPlanetData(), Edits = Terrain->GetPublishedEditLayers()]()
	{
		TSharedPtr<FTerrainBake> Bake = MakeShared<FTerrainBake>(BakeTerrainPatch(Planet, Edits, Center, Radius * BakeExtentScale, UnitsPerMetre, Exaggeration));
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, Center, Radius, Bake]()
		{
			ASWGHoloMapActor* Actor = WeakThis.Get();
			if (Actor && Actor->BakeGeneration == Generation)
			{
				Actor->ApplyBake(MoveTemp(*Bake), Center, Radius);
			}
		});
	});
}

void ASWGHoloMapActor::ApplyBake(FTerrainBake&& Bake, const FVector2D& Center, float Radius)
{
	bBakeInFlight = false;
	if (!Bake.Mesh.IsValid())
	{
		return;
	}
	const bool bMovedFar = !bHasBake || FVector2D::Distance(Center, BakedCenter) > 1.f || !FMath::IsNearlyEqual(Radius, BakedRadius);
	BakedCenter = Center;
	BakedRadius = Radius;
	BakedBaseHeight = Bake.BaseHeight;
	BakedHeights = MoveTemp(Bake.Heights);
	bHasBake = true;
	TerrainComponent->SetMesh(MoveTemp(*Bake.Mesh));
	TerrainComponent->SetMaterial(0, ContentMaterial);
	UpdateContentTransform();
	if (bMovedFar)
	{
		RequestBuildings();
	}
	RefreshDynamicStructures(/*bRebuild=*/true);
	UpdateMarkers();
}

void ASWGHoloMapActor::RefreshDynamicStructures(bool bRebuild)
{
	NextDynamicScanTime = FPlatformTime::Seconds() + DynamicStructureScanSeconds;
	if (bRebuild)
	{
		TArray<TWeakObjectPtr<AActor>> Mirrored;
		DynamicStructures.GetKeys(Mirrored);
		for (const TWeakObjectPtr<AActor>& Structure : Mirrored)
		{
			RemoveDynamicStructure(Structure);
		}
	}
	// Gone from memory (demolished, or streamed out of range).
	TArray<TWeakObjectPtr<AActor>> Stale;
	for (const TPair<TWeakObjectPtr<AActor>, TArray<TWeakObjectPtr<UStaticMeshComponent>>>& Pair : DynamicStructures)
	{
		if (!Pair.Key.IsValid())
		{
			Stale.Add(Pair.Key);
		}
	}
	for (const TWeakObjectPtr<AActor>& Structure : Stale)
	{
		RemoveDynamicStructure(Structure);
	}
	if (!bHasBake)
	{
		return;
	}

	const float Extent = BakedRadius * BakeExtentScale;
	for (TActorIterator<ASWGObject> It(GetWorld()); It; ++It)
	{
		ASWGObject* Structure = *It;
		// Snapshot buildings come from the .ws through RequestBuildings; only server-sent ones here.
		const bool bStructure = Structure->IsA<ASWGBuilding>() || Structure->IsA<ASWGInstallation>();
		if (!bStructure || !Structure->StaticTemplatePath.IsEmpty() || DynamicStructures.Contains(Structure))
		{
			continue;
		}
		const FVector Raw = SWGToRawSpace(Structure->GetActorLocation());
		if (FVector2D::Distance(FVector2D(Raw.X, Raw.Y), BakedCenter) <= Extent)
		{
			AddDynamicStructure(*Structure);
		}
	}
}

void ASWGHoloMapActor::AddDynamicStructure(AActor& Structure)
{
	TArray<TWeakObjectPtr<UStaticMeshComponent>>& Copies = DynamicStructures.Add(&Structure);
	TArray<UStaticMeshComponent*> Sources;
	Structure.GetComponents(Sources);
	const float MeshScale = BakedUnitsPerMetre() / SWGWorldScale * BuildingScale;
	for (const UStaticMeshComponent* Source : Sources)
	{
		UStaticMesh* Mesh = Source->GetStaticMesh();
		// Collision-only helpers are invisible; only what the player sees goes on the map.
		if (!Mesh || !Source->IsVisible())
		{
			continue;
		}
		const FTransform World = Source->GetComponentTransform();
		const FVector Raw = SWGToRawSpace(World.GetLocation());
		const FVector Local = RawToContent(FVector(Raw.X - BakedCenter.X, Raw.Y - BakedCenter.Y, Raw.Z));
		const FVector Scale = World.GetScale3D() * FVector(MeshScale, MeshScale, MeshScale * HeightExaggeration);

		UStaticMeshComponent* Copy = NewObject<UStaticMeshComponent>(this);
		Copy->SetStaticMesh(Mesh);
		Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Copy->SetCastShadow(false);
		Copy->SetForcedLodModel(Mesh->GetNumLODs());
		for (int32 MaterialIndex = 0; MaterialIndex < Mesh->GetStaticMaterials().Num(); ++MaterialIndex)
		{
			Copy->SetMaterial(MaterialIndex, ContentMaterial);
		}
		Copy->SetupAttachment(ContentRoot);
		Copy->SetRelativeTransform(FTransform(World.GetRotation(), Local, Scale));
		Copy->RegisterComponent();
		BuildingComponents.Add(Copy);
		Copies.Add(Copy);
	}
}

void ASWGHoloMapActor::RemoveDynamicStructure(const TWeakObjectPtr<AActor>& Structure)
{
	if (const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Copies = DynamicStructures.Find(Structure))
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& Copy : *Copies)
		{
			if (Copy.IsValid())
			{
				BuildingComponents.Remove(Copy.Get());
				Copy->DestroyComponent();
			}
		}
	}
	DynamicStructures.Remove(Structure);
}

void ASWGHoloMapActor::ClearBuildings()
{
	for (UStaticMeshComponent* Building : BuildingComponents)
	{
		if (Building)
		{
			Building->DestroyComponent();
		}
	}
	BuildingComponents.Reset();
}

void ASWGHoloMapActor::RequestBuildings()
{
	ClearBuildings();
	const int32 Generation = ++BuildingGeneration;
	UGameInstance* GameInstance = GetGameInstance();
	const USWGTerrainSubsystem* Terrain = GameInstance ? GameInstance->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
	USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	const TSharedPtr<const FSWGWorldSnapshotData, ESPMode::ThreadSafe> Snapshot = Terrain ? Terrain->GetSnapshotData() : nullptr;
	if (!Snapshot || !MeshGenerator)
	{
		return;
	}

	const float Extent = BakedRadius * BakeExtentScale;
	TArray<TPair<float, int32>> Candidates;
	for (int32 NodeIndex = 0; NodeIndex < Snapshot->Nodes.Num(); ++NodeIndex)
	{
		const FSWGWorldSnapshotNode& Node = Snapshot->Nodes[NodeIndex];
		const float DistanceSquared = FVector2D::DistSquared(FVector2D(Node.Position.X, Node.Position.Y), BakedCenter);
		if (DistanceSquared <= FMath::Square(Extent) && Snapshot->ObjectTemplateNames.IsValidIndex((int32)Node.NameID)
			&& Snapshot->ObjectTemplateNames[(int32)Node.NameID].StartsWith(TEXT("object/building/")))
		{
			Candidates.Emplace(DistanceSquared, NodeIndex);
		}
	}
	Candidates.Sort([](const TPair<float, int32>& Left, const TPair<float, int32>& Right) { return Left.Key < Right.Key; });

	// One request per template; cities reuse a handful of building types.
	TMap<FString, TArray<FTransform>> TransformsByTemplate;
	const float MeshScale = BakedUnitsPerMetre() / SWGWorldScale * BuildingScale;
	for (int32 CandidateIndex = 0; CandidateIndex < FMath::Min(Candidates.Num(), MaxBuildings); ++CandidateIndex)
	{
		const FSWGWorldSnapshotNode& Node = Snapshot->Nodes[Candidates[CandidateIndex].Value];
		const FVector Local = RawToContent(FVector(Node.Position.X - BakedCenter.X, Node.Position.Y - BakedCenter.Y, Node.Position.Z));
		// Same rotation the streamed snapshot uses; heights exaggerated like the terrain under them.
		TransformsByTemplate.FindOrAdd(Snapshot->ObjectTemplateNames[(int32)Node.NameID])
			.Add(FTransform(Node.Direction, Local, FVector(MeshScale, MeshScale, MeshScale * HeightExaggeration)));
	}

	TWeakObjectPtr<ASWGHoloMapActor> WeakThis(this);
	for (TPair<FString, TArray<FTransform>>& Pair : TransformsByTemplate)
	{
		MeshGenerator->RequestTemplateStaticMesh(Pair.Key,
			[WeakThis, Generation, Transforms = MoveTemp(Pair.Value)](UStaticMesh* Mesh, const TArray<UMaterialInterface*>&)
			{
				ASWGHoloMapActor* Actor = WeakThis.Get();
				if (!Actor || !Mesh || Actor->BuildingGeneration != Generation)
				{
					return;
				}
				for (const FTransform& Transform : Transforms)
				{
					UStaticMeshComponent* Building = NewObject<UStaticMeshComponent>(Actor);
					Building->SetStaticMesh(Mesh);
					Building->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Building->SetCastShadow(false);
					// 1-based; the last level is the low-poly silhouette.
					Building->SetForcedLodModel(Mesh->GetNumLODs());
					for (int32 MaterialIndex = 0; MaterialIndex < Mesh->GetStaticMaterials().Num(); ++MaterialIndex)
					{
						Building->SetMaterial(MaterialIndex, Actor->ContentMaterial);
					}
					Building->SetupAttachment(Actor->ContentRoot);
					Building->SetRelativeTransform(Transform);
					Building->RegisterComponent();
					Actor->BuildingComponents.Add(Building);
				}
			});
	}
}

void ASWGHoloMapActor::SetMarkers(FName Layer, const TArray<FSWGMapMarker>& Markers)
{
	TArray<FMarkerVisual>& Visuals = MarkerLayers.FindOrAdd(Layer);
	// Same markers restyled or moved: update in place (the player marker moves every frame).
	bool bInPlace = Visuals.Num() == Markers.Num();
	for (int32 Index = 0; bInPlace && Index < Markers.Num(); ++Index)
	{
		bInPlace = Visuals[Index].Marker.LooksLike(Markers[Index]);
	}
	if (bInPlace)
	{
		for (int32 Index = 0; Index < Markers.Num(); ++Index)
		{
			Visuals[Index].Marker = Markers[Index];
		}
		return;
	}
	for (FMarkerVisual& Visual : Visuals)
	{
		for (UPrimitiveComponent* Component : { (UPrimitiveComponent*)Visual.Beam, (UPrimitiveComponent*)Visual.Label, (UPrimitiveComponent*)Visual.Locator, (UPrimitiveComponent*)Visual.Ping })
		{
			if (Component) { Component->DestroyComponent(); }
		}
	}
	Visuals.Reset();
	for (const FSWGMapMarker& Marker : Markers)
	{
		FMarkerVisual& Visual = Visuals.AddDefaulted_GetRef();
		Visual.Marker = Marker;
		const bool bPlayer = Marker.Style == HoloPlayerStyle;
		const FLinearColor Color = Marker.bCustomPinColor ? Marker.PinColor : (bPlayer ? FLinearColor(0.6f, 0.95f, 1.f) : FLinearColor(1.f, 0.45f, 0.1f));

		Visual.Beam = NewObject<UStaticMeshComponent>(this);
		Visual.Beam->SetStaticMesh(bPlayer ? ArrowMesh : BeamMesh);
		Visual.Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual.Beam->SetCastShadow(false);
		Visual.Beam->SetMaterial(0, MakeHoloMaterial(Color, HoloIntensity * (bPlayer ? PlayerMarkerGlow : 2.f)));
		Visual.Beam->SetupAttachment(GetRootComponent());
		Visual.Beam->RegisterComponent();
		MarkerObjects.Add(Visual.Beam);

		if (bPlayer)
		{
			auto MakePlayerPart = [this](UMaterialInterface* Material)
				{
					UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
					Part->SetStaticMesh(BeamMesh);
					Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Part->SetCastShadow(false);
					Part->SetMaterial(0, Material);
					Part->SetupAttachment(GetRootComponent());
					Part->RegisterComponent();
					MarkerObjects.Add(Part);
					return Part;
				};
			Visual.Locator = MakePlayerPart(MakeHoloMaterial(Color, HoloIntensity * PlayerMarkerGlow));
			Visual.PingMaterial = MakeHoloMaterial(Color, HoloIntensity * PlayerMarkerGlow);
			MarkerObjects.Add(Visual.PingMaterial);
			Visual.Ping = MakePlayerPart(Visual.PingMaterial);
		}

		if (!Marker.Label.IsEmpty())
		{
			Visual.Label = NewObject<UTextRenderComponent>(this);
			Visual.Label->SetText(Marker.Label);
			Visual.Label->SetTextRenderColor(Marker.LabelColor.ToFColor(true));
			Visual.Label->SetWorldSize(LabelWorldSize);
			Visual.Label->SetHorizontalAlignment(EHTA_Center);
			Visual.Label->SetVerticalAlignment(EVRTA_TextBottom);
			Visual.Label->SetupAttachment(GetRootComponent());
			Visual.Label->RegisterComponent();
			MarkerObjects.Add(Visual.Label);
		}
	}
	UpdateMarkers();
}

void ASWGHoloMapActor::UpdateMarkers()
{
	const APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	if (PlayerController)
	{
		PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	}
	const FTransform ContentToWorld = ContentRoot->GetComponentTransform();
	const float BeamHeight = DiscDiameter * 0.12f;

	struct FPlacedMarker
	{
		FMarkerVisual* Visual;
		FVector Ground;
		/** Zero for markers that don't take part in staggering. */
		float LabelHalfWidth = 0.f;
		int32 Tier = 0;
	};
	TArray<FPlacedMarker> Placed;
	for (TPair<FName, TArray<FMarkerVisual>>& Layer : MarkerLayers)
	{
		for (FMarkerVisual& Visual : Layer.Value)
		{
			const FSWGMapMarker& Marker = Visual.Marker;
			const bool bOnDisc = bHasBake && FVector2D::Distance(Marker.Position, ViewCenter) <= ViewRadius;
			for (UPrimitiveComponent* Component : { (UPrimitiveComponent*)Visual.Beam, (UPrimitiveComponent*)Visual.Label, (UPrimitiveComponent*)Visual.Locator, (UPrimitiveComponent*)Visual.Ping })
			{
				if (Component) { Component->SetVisibility(bOnDisc); }
			}
			if (bOnDisc)
			{
				const bool bStaggered = Visual.Label && Marker.Style != HoloPlayerStyle;
				Placed.Add({ &Visual, ContentToWorld.TransformPosition(RawToContent(FVector(Marker.Position - BakedCenter, BakedHeightAt(Marker.Position)))),
					bStaggered ? LabelWorldSize * 0.3f * Marker.Label.ToString().Len() : 0.f });
			}
		}
	}

	// Stagger crowded labels upward: each takes the lowest tier where it clears
	// every label already placed. Labels turn to face the camera, so the
	// footprint is the text's half-width in any direction. A fixed order keeps
	// tiers from flickering as the view moves.
	Placed.Sort([](const FPlacedMarker& Left, const FPlacedMarker& Right)
		{
			return Left.Visual->Marker.Position.X != Right.Visual->Marker.Position.X
				? Left.Visual->Marker.Position.X < Right.Visual->Marker.Position.X
				: Left.Visual->Marker.Position.Y < Right.Visual->Marker.Position.Y;
		});
	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		FPlacedMarker& Current = Placed[Index];
		if (Current.LabelHalfWidth <= 0.f)
		{
			continue;
		}
		for (bool bBlocked = true; bBlocked && Current.Tier < MaxLabelTiers; )
		{
			bBlocked = false;
			for (int32 Other = 0; Other < Index && !bBlocked; ++Other)
			{
				const FPlacedMarker& Neighbour = Placed[Other];
				bBlocked = Neighbour.LabelHalfWidth > 0.f && Neighbour.Tier == Current.Tier
					&& FVector::Dist2D(Neighbour.Ground, Current.Ground) < Current.LabelHalfWidth + Neighbour.LabelHalfWidth;
			}
			Current.Tier += bBlocked ? 1 : 0;
		}
	}

	for (const FPlacedMarker& Entry : Placed)
	{
		FMarkerVisual& Visual = *Entry.Visual;
		const FSWGMapMarker& Marker = Visual.Marker;
		const FVector& Ground = Entry.Ground;
		const bool bPlayer = Marker.Style == HoloPlayerStyle;
		const float StemHeight = BeamHeight + Entry.Tier * LabelTierStep;
		if (bPlayer)
		{
			// A cone lying flat, tip toward the heading, hovering just over the ground and breathing gently.
			const float Pulse = 0.5f + 0.5f * FMath::Sin(DroidTime * UE_TWO_PI / PlayerPulseSeconds);
			Visual.Beam->SetWorldLocation(Ground + FVector(0.f, 0.f, 4.f));
			Visual.Beam->SetWorldRotation(FRotator(-90.f, Marker.Heading + ViewYaw, 0.f));
			Visual.Beam->SetWorldScale3D(FVector(0.07f, 0.07f, 0.1f) * (1.f + 0.15f * Pulse));
			if (Visual.Locator)
			{
				const float LocatorHeight = BeamHeight * 2.2f;
				Visual.Locator->SetWorldLocation(Ground + FVector(0.f, 0.f, LocatorHeight * 0.5f));
				Visual.Locator->SetWorldScale3D(FVector(0.02f, 0.02f, LocatorHeight / 100.f));
			}
			if (Visual.Ping)
			{
				// Expands from under the arrow and fades out, then starts again. Engine cylinders are 100 units across.
				const float Phase = FMath::Frac(DroidTime / PlayerPulseSeconds);
				const float PingRadius = 2.f + Phase * 16.f;
				Visual.Ping->SetWorldLocation(Ground + FVector(0.f, 0.f, 1.f));
				Visual.Ping->SetWorldScale3D(FVector(PingRadius / 50.f, PingRadius / 50.f, 0.002f));
				Visual.PingMaterial->SetScalarParameterValue(TEXT("Intensity"), HoloIntensity * PlayerMarkerGlow * (1.f - Phase) * 0.6f);
			}
		}
		else
		{
			// A thin beam standing on the point, reaching its label; engine cylinders are 100 units tall about their centre.
			Visual.Beam->SetWorldLocation(Ground + FVector(0.f, 0.f, StemHeight * 0.5f));
			Visual.Beam->SetWorldRotation(FRotator::ZeroRotator);
			Visual.Beam->SetWorldScale3D(FVector(0.012f, 0.012f, StemHeight / 100.f));
		}
		if (Visual.Label)
		{
			Visual.Label->SetWorldLocation(Ground + FVector(0.f, 0.f, bPlayer ? 6.f : StemHeight + 1.f));
			// Face the viewer; text renders along its component's +X.
			const FVector ToCamera = (CameraLocation - Visual.Label->GetComponentLocation()).GetSafeNormal2D();
			Visual.Label->SetWorldRotation(ToCamera.Rotation());
		}
	}
}

bool ASWGHoloMapActor::RayToRaw(const FVector& Origin, const FVector& Direction, FVector2D& OutRaw, bool bRequireOnDisc) const
{
	if (!bHasBake || FMath::IsNearlyZero(Direction.Z))
	{
		return false;
	}
	const FTransform ContentToWorld = ContentRoot->GetComponentTransform();
	const float PlaneZ = ContentToWorld.TransformPosition(RawToContent(FVector(ViewCenter - BakedCenter, BakedHeightAt(ViewCenter)))).Z;
	const float Distance = (PlaneZ - Origin.Z) / Direction.Z;
	if (Distance <= 0.f)
	{
		return false;
	}
	const FVector Local = ContentToWorld.InverseTransformPosition(Origin + Direction * Distance);
	// Content X is north, Y is east (raw y, x).
	OutRaw = BakedCenter + FVector2D(Local.Y, Local.X) / BakedUnitsPerMetre();
	return !bRequireOnDisc || FVector2D::Distance(OutRaw, ViewCenter) <= ViewRadius;
}

FVector ASWGHoloMapActor::GetFocusLocation() const
{
	if (!bHasBake)
	{
		return GetActorLocation() + FVector(0.f, 0.f, ContentLift);
	}
	return ContentRoot->GetComponentTransform().TransformPosition(RawToContent(FVector(ViewCenter - BakedCenter, BakedHeightAt(ViewCenter))));
}
