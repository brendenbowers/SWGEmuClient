#include "Map/SWGPlanetMapScene.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Common/SWGWorldScale.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGTerrainEvaluator.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGWorldSnapshotReader.h"

namespace
{
	/** Relief texture edge; the mesh takes every other texel, so a 32 m grid on a 16 km planet. */
	constexpr int32 ReliefSize = 1024;
	constexpr int32 GridStride = 2;
	constexpr int32 GridSize = ReliefSize / GridStride;
	constexpr float FieldOfView = 50.f;
	constexpr float BuildingRadius = 900.f;
	constexpr int32 MaxBuildings = 250;
	/** Captures kept going after the view stops, so temporal AA converges instead of freezing mid-jitter. */
	constexpr int32 SettleFrames = 6;

	FColor SurfaceColor(const FString& Name)
	{
		if (Name.Contains(TEXT("water")) || Name.Contains(TEXT("ocean"))) return FColor(35, 79, 105);
		if (Name.Contains(TEXT("snow")) || Name.Contains(TEXT("ice"))) return FColor(206, 213, 208);
		if (Name.Contains(TEXT("sand")) || Name.Contains(TEXT("desert"))) return FColor(174, 150, 105);
		if (Name.Contains(TEXT("grass")) || Name.Contains(TEXT("forest")) || Name.Contains(TEXT("jungle"))) return FColor(75, 115, 73);
		if (Name.Contains(TEXT("lava")) || Name.Contains(TEXT("volcan"))) return FColor(113, 64, 54);
		if (Name.Contains(TEXT("rock")) || Name.Contains(TEXT("cliff"))) return FColor(115, 111, 101);
		if (Name.Contains(TEXT("tatooine")) || Name.Contains(TEXT("lok"))) return FColor(170, 145, 105);
		if (Name.Contains(TEXT("naboo")) || Name.Contains(TEXT("corellia"))) return FColor(82, 123, 83);
		return FColor(124, 119, 100);
	}

	FVector RawToUnrealNormal(const FVector& RawNormal)
	{
		return FVector(RawNormal.Y, RawNormal.X, RawNormal.Z).GetSafeNormal();
	}
}

float FSWGPlanetMapCamera::GetPitch() const
{
	const float ZoomFraction = FMath::Clamp(FMath::Loge(Distance / MinDistance) / FMath::Loge(MaxDistance / MinDistance), 0.f, 1.f);
	const float ZoomPitch = FMath::Lerp(-38.f, -89.f, FMath::SmoothStep(0.f, 1.f, ZoomFraction));
	return FMath::Clamp(ZoomPitch + Tilt, -89.f, -12.f);
}

FSWGPlanetMapCamera FSWGPlanetMapCamera::Blend(const FSWGPlanetMapCamera& From, const FSWGPlanetMapCamera& To, float Alpha)
{
	FSWGPlanetMapCamera Result;
	Result.Target = FMath::Lerp(From.Target, To.Target, Alpha);
	Result.Distance = FMath::Exp(FMath::Lerp(FMath::Loge(From.Distance), FMath::Loge(To.Distance), Alpha));
	Result.Yaw = FRotator::NormalizeAxis(From.Yaw + FMath::FindDeltaAngleDegrees(From.Yaw, To.Yaw) * Alpha);
	Result.Tilt = FMath::Lerp(From.Tilt, To.Tilt, Alpha);
	return Result;
}

bool FSWGPlanetMapCamera::IsNearlyEqual(const FSWGPlanetMapCamera& Other) const
{
	return FVector2D::DistSquared(Target, Other.Target) < FMath::Square(Distance * 0.0005f)
		&& FMath::IsNearlyEqual(Distance, Other.Distance, Distance * 0.001f)
		&& FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, Other.Yaw)) < 0.05f
		&& FMath::IsNearlyEqual(Tilt, Other.Tilt, 0.05f);
}

FSWGPlanetMapScene::FSWGPlanetMapScene(UGameInstance* GameInstance)
	: PreviewScene(FPreviewScene::ConstructionValues()
		.SetEditor(false)
		.SetTransactional(false)
		.SetCreatePhysicsScene(false)
		// From the north-west, the cartographic hillshade convention.
		.SetLightRotation(FRotator(-50.f, 135.f, 0.f))
		.SetLightBrightness(UE_PI)
		.SetSkyBrightness(0.f))
{
	Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	MeshGenerator = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;

	// The default sky light has no cubemap to light from, so a shadowless cool
	// fill from the opposite side stands in for ambient.
	UDirectionalLightComponent* FillLight = NewObject<UDirectionalLightComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	FillLight->Intensity = UE_PI * 0.35f;
	FillLight->LightColor = FColor(170, 190, 225);
	FillLight->CastShadows = false;
	PreviewScene.AddComponent(FillLight, FTransform(FRotator(-60.f, -45.f, 0.f)));

	RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	// Float, so the tonemapped output is stored linear and UMG's sRGB draw is correct.
	RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	RenderTarget->ClearColor = FLinearColor::Black;
	RenderTarget->InitAutoFormat(ViewportSize.X, ViewportSize.Y);
	RenderTarget->UpdateResourceImmediate(true);

	Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Capture->ProjectionType = ECameraProjectionMode::Perspective;
	Capture->FOVAngle = FieldOfView;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->bOverride_CustomNearClippingPlane = true;
	Capture->CustomNearClippingPlane = SWGToUnrealSpace(10.f);
	Capture->TextureTarget = RenderTarget;
	FPostProcessSettings& PostProcess = Capture->PostProcessSettings;
	PostProcess.bOverride_AutoExposureMethod = true;
	PostProcess.AutoExposureMethod = AEM_Manual;
	PostProcess.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PostProcess.AutoExposureApplyPhysicalCameraExposure = false;
	PostProcess.bOverride_AutoExposureBias = true;
	PostProcess.AutoExposureBias = 0.f;
	PostProcess.bOverride_DynamicGlobalIlluminationMethod = true;
	PostProcess.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	PostProcess.bOverride_ReflectionMethod = true;
	PostProcess.ReflectionMethod = EReflectionMethod::None;
	PreviewScene.AddComponent(Capture, FTransform::Identity);

	// After registration, which resets show flags from the archetype.
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetBloom(false);
}

FSWGPlanetMapScene::~FSWGPlanetMapScene() = default;

void FSWGPlanetMapScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Tre);
	Collector.AddReferencedObject(MeshGenerator);
	Collector.AddReferencedObject(Capture);
	Collector.AddReferencedObject(RenderTarget);
	Collector.AddReferencedObject(TerrainComponent);
	Collector.AddReferencedObject(ReliefTexture);
	Collector.AddReferencedObject(TerrainMaterial);
	Collector.AddReferencedObjects(BuildingComponents);
}

void FSWGPlanetMapScene::ShowPlanet(const FString& InPlanet, const TArray<FVector2D>& InFocusPoints)
{
	check(IsInGameThread());
	const bool bNewPlanet = Planet != InPlanet;
	if (!bNewPlanet && FocusPoints == InFocusPoints)
	{
		return;
	}
	Planet = InPlanet;
	FocusPoints = InFocusPoints;
	ClearBuildings();
	++BuildingGeneration;
	if (!Tre)
	{
		return;
	}

	const TWeakPtr<FSWGPlanetMapScene> WeakThis = AsShared();
	if (bNewPlanet)
	{
		ClearTerrain();
		const int32 Generation = ++TerrainGeneration;
		TArray<uint8> TerrainBytes = Tre->ExtractFile(FString::Printf(TEXT("terrain/%s.trn"), *Planet));
		if (!TerrainBytes.IsEmpty())
		{
			Async(EAsyncExecution::Thread, [WeakThis, Generation, PlanetName = Planet, TerrainBytes = MoveTemp(TerrainBytes)]() mutable
			{
				TSharedPtr<FTerrainBake> Bake = MakeShared<FTerrainBake>(BakeTerrain(MoveTemp(TerrainBytes), PlanetName));
				AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, Bake]()
				{
					const TSharedPtr<FSWGPlanetMapScene> Scene = WeakThis.Pin();
					if (Scene && Scene->TerrainGeneration == Generation)
					{
						Scene->ApplyTerrain(MoveTemp(*Bake));
					}
				});
			});
		}
	}

	TArray<uint8> SnapshotBytes = Tre->ExtractFile(FString::Printf(TEXT("snapshot/%s.ws"), *Planet));
	if (SnapshotBytes.IsEmpty() || FocusPoints.IsEmpty() || !MeshGenerator)
	{
		return;
	}
	const int32 Generation = BuildingGeneration;
	Async(EAsyncExecution::Thread, [WeakThis, Generation, Points = FocusPoints, SnapshotBytes = MoveTemp(SnapshotBytes)]() mutable
	{
		TArray<FBuildingPlacement> Placements = SelectBuildings(MoveTemp(SnapshotBytes), Points);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, Placements = MoveTemp(Placements)]() mutable
		{
			const TSharedPtr<FSWGPlanetMapScene> Scene = WeakThis.Pin();
			if (Scene && Scene->BuildingGeneration == Generation)
			{
				Scene->RequestBuildings(MoveTemp(Placements));
			}
		});
	});
}

void FSWGPlanetMapScene::ClearTerrain()
{
	if (TerrainComponent)
	{
		PreviewScene.RemoveComponent(TerrainComponent);
		TerrainComponent = nullptr;
	}
	Heights.Reset();
	bDirty = true;
}

void FSWGPlanetMapScene::ClearBuildings()
{
	for (UStaticMeshComponent* Building : BuildingComponents)
	{
		PreviewScene.RemoveComponent(Building);
	}
	BuildingComponents.Reset();
	bDirty = true;
}

FSWGPlanetMapScene::FTerrainBake FSWGPlanetMapScene::BakeTerrain(TArray<uint8>&& TerrainBytes, const FString& PlanetName)
{
	using namespace UE::Geometry;

	FTerrainBake Bake;
	FSWGTerrainData Terrain;
	if (!FSWGTerrainReader::ReadTerrain(FSWGIffReader(MoveTemp(TerrainBytes)), Terrain))
	{
		return Bake;
	}

	TMap<int32, FColor> Palette;
	for (const FSWGShaderFamily& Family : Terrain.ShaderFamilies)
	{
		Palette.Add(Family.FamilyId, SurfaceColor(Family.Name.ToLower()));
	}
	const bool bHasWater = Terrain.Header.bUseGlobalWaterTable;
	const float WaterHeight = Terrain.Header.GlobalWaterTableHeight;
	const float PlanetSize = Terrain.Header.MapSize;
	const float HalfMap = PlanetSize * 0.5f;

	TArray<float> ReliefHeights;
	ReliefHeights.SetNumUninitialized(ReliefSize * ReliefSize);
	Bake.Pixels.SetNumUninitialized(ReliefSize * ReliefSize);
	// Rows in parallel: the evaluator only reads the immutable planet data.
	ParallelFor(ReliefSize, [&](int32 PixelY)
	{
		for (int32 PixelX = 0; PixelX < ReliefSize; ++PixelX)
		{
			const float RawX = ((PixelX + 0.5f) / ReliefSize) * PlanetSize - HalfMap;
			const float RawY = HalfMap - ((PixelY + 0.5f) / ReliefSize) * PlanetSize;
			const int32 PixelIndex = PixelY * ReliefSize + PixelX;
			const float Height = FSWGTerrainEvaluator::GetHeight(Terrain, RawX, RawY);
			ReliefHeights[PixelIndex] = bHasWater ? FMath::Max(Height, WaterHeight) : Height;
			if (bHasWater && Height < WaterHeight)
			{
				Bake.Pixels[PixelIndex] = SurfaceColor(TEXT("water"));
				continue;
			}
			TMap<int32, float> Weights;
			FSWGTerrainEvaluator::GetShaderWeights(Terrain, RawX, RawY, Weights);
			int32 DominantFamily = 0;
			float Strength = 0.f;
			for (const TPair<int32, float>& Weight : Weights)
			{
				if (Weight.Value > Strength) { DominantFamily = Weight.Key; Strength = Weight.Value; }
			}
			const FColor* FamilyColor = Palette.Find(DominantFamily);
			Bake.Pixels[PixelIndex] = FamilyColor ? *FamilyColor : SurfaceColor(PlanetName);
		}
	});

	// Mild baked hillshade for detail finer than the mesh grid; the lights do the rest.
	for (int32 PixelY = 1; PixelY < ReliefSize - 1; ++PixelY)
	{
		for (int32 PixelX = 1; PixelX < ReliefSize - 1; ++PixelX)
		{
			const int32 PixelIndex = PixelY * ReliefSize + PixelX;
			const float Slope = ReliefHeights[PixelIndex - 1] - ReliefHeights[PixelIndex + 1]
				+ ReliefHeights[PixelIndex - ReliefSize] - ReliefHeights[PixelIndex + ReliefSize];
			const float Light = FMath::Clamp(0.95f + Slope * 0.01f, 0.75f, 1.12f);
			FColor& Pixel = Bake.Pixels[PixelIndex];
			Pixel.R = FMath::Clamp(FMath::RoundToInt(Pixel.R * Light), 0, 255);
			Pixel.G = FMath::Clamp(FMath::RoundToInt(Pixel.G * Light), 0, 255);
			Pixel.B = FMath::Clamp(FMath::RoundToInt(Pixel.B * Light), 0, 255);
		}
	}
	for (FColor& Pixel : Bake.Pixels)
	{
		Pixel.A = 255;
	}

	Bake.MapSize = PlanetSize;
	Bake.Heights.SetNumUninitialized(GridSize * GridSize);
	for (int32 Row = 0; Row < GridSize; ++Row)
	{
		for (int32 Column = 0; Column < GridSize; ++Column)
		{
			Bake.Heights[Row * GridSize + Column] = ReliefHeights[(Row * GridStride) * ReliefSize + Column * GridStride];
		}
	}

	// Rows run south from the north edge, matching the texture, so vertex UVs are texel centres.
	const float GridSpacing = PlanetSize / ReliefSize * GridStride;
	Bake.Mesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
	FDynamicMesh3& Mesh = *Bake.Mesh;
	Mesh.EnableAttributes();
	FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
	FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->PrimaryUV();
	auto HeightAt = [&Bake](int32 Column, int32 Row)
	{
		return Bake.Heights[FMath::Clamp(Row, 0, GridSize - 1) * GridSize + FMath::Clamp(Column, 0, GridSize - 1)];
	};
	for (int32 Row = 0; Row < GridSize; ++Row)
	{
		for (int32 Column = 0; Column < GridSize; ++Column)
		{
			const int32 PixelX = Column * GridStride;
			const int32 PixelY = Row * GridStride;
			const FVector RawPosition(((PixelX + 0.5f) / ReliefSize) * PlanetSize - HalfMap,
				HalfMap - ((PixelY + 0.5f) / ReliefSize) * PlanetSize, HeightAt(Column, Row));
			Mesh.AppendVertex(FVector3d(SWGToUnrealSpace(RawPosition)));
			const float SlopeEast = (HeightAt(Column + 1, Row) - HeightAt(Column - 1, Row)) / (2.f * GridSpacing);
			const float SlopeNorth = (HeightAt(Column, Row - 1) - HeightAt(Column, Row + 1)) / (2.f * GridSpacing);
			Normals->AppendElement(FVector3f(RawToUnrealNormal(FVector(-SlopeEast, -SlopeNorth, 1.f))));
			UVs->AppendElement(FVector2f((PixelX + 0.5f) / ReliefSize, (PixelY + 0.5f) / ReliefSize));
		}
	}
	for (int32 Row = 0; Row < GridSize - 1; ++Row)
	{
		for (int32 Column = 0; Column < GridSize - 1; ++Column)
		{
			const int32 NorthWest = Row * GridSize + Column;
			const int32 NorthEast = NorthWest + 1;
			const int32 SouthWest = NorthWest + GridSize;
			const int32 SouthEast = SouthWest + 1;
			// Rows step south (UE -X), the reverse of the streamed tiles, so the winding flips too.
			for (const FIndex3i& Triangle : { FIndex3i(NorthWest, SouthWest, NorthEast), FIndex3i(NorthEast, SouthWest, SouthEast) })
			{
				const int32 TriangleId = Mesh.AppendTriangle(Triangle);
				if (TriangleId >= 0)
				{
					Normals->SetTriangle(TriangleId, Triangle);
					UVs->SetTriangle(TriangleId, Triangle);
				}
			}
		}
	}
	return Bake;
}

void FSWGPlanetMapScene::ApplyTerrain(FTerrainBake&& Bake)
{
	if (!Bake.Mesh.IsValid() || Bake.Pixels.Num() != ReliefSize * ReliefSize)
	{
		return;
	}
	MapSize = Bake.MapSize;
	Heights = MoveTemp(Bake.Heights);

	ReliefTexture = UTexture2D::CreateTransient(ReliefSize, ReliefSize, PF_B8G8R8A8);
	if (ReliefTexture)
	{
		FTexture2DMipMap& Mip = ReliefTexture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Bake.Pixels.GetData(), Bake.Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		ReliefTexture->SRGB = true;
		ReliefTexture->Filter = TF_Bilinear;
		ReliefTexture->AddressX = TA_Clamp;
		ReliefTexture->AddressY = TA_Clamp;
		ReliefTexture->UpdateResource();
	}
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGObjectTextured.M_SWGObjectTextured"));
	TerrainMaterial = Parent ? UMaterialInstanceDynamic::Create(Parent, GetTransientPackage()) : nullptr;
	if (TerrainMaterial)
	{
		TerrainMaterial->SetTextureParameterValue(TEXT("Diffuse"), ReliefTexture);
	}

	TerrainComponent = NewObject<UDynamicMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	TerrainComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TerrainComponent->SetMesh(MoveTemp(*Bake.Mesh));
	TerrainComponent->SetMaterial(0, TerrainMaterial);
	PreviewScene.AddComponent(TerrainComponent, FTransform::Identity);
	bDirty = true;
}

TArray<FSWGPlanetMapScene::FBuildingPlacement> FSWGPlanetMapScene::SelectBuildings(TArray<uint8>&& SnapshotBytes, const TArray<FVector2D>& Points)
{
	TArray<FBuildingPlacement> Placements;
	FSWGWorldSnapshotData Snapshot;
	if (!FSWGWorldSnapshotReader::ReadWorldSnapshot(FSWGIffReader(MoveTemp(SnapshotBytes)), Snapshot))
	{
		return Placements;
	}
	TArray<TPair<float, int32>> Candidates;
	for (int32 NodeIndex = 0; NodeIndex < Snapshot.Nodes.Num(); ++NodeIndex)
	{
		const FSWGWorldSnapshotNode& Node = Snapshot.Nodes[NodeIndex];
		if (!Snapshot.ObjectTemplateNames.IsValidIndex((int32)Node.NameID)
			|| !Snapshot.ObjectTemplateNames[(int32)Node.NameID].StartsWith(TEXT("object/building/")))
		{
			continue;
		}
		float NearestSquared = TNumericLimits<float>::Max();
		for (const FVector2D& Point : Points)
		{
			NearestSquared = FMath::Min(NearestSquared, (float)FVector2D::DistSquared(Point, FVector2D(Node.Position.X, Node.Position.Y)));
		}
		if (NearestSquared <= FMath::Square(BuildingRadius))
		{
			Candidates.Emplace(NearestSquared, NodeIndex);
		}
	}
	Candidates.Sort([](const TPair<float, int32>& Left, const TPair<float, int32>& Right) { return Left.Key < Right.Key; });
	for (int32 CandidateIndex = 0; CandidateIndex < FMath::Min(Candidates.Num(), MaxBuildings); ++CandidateIndex)
	{
		const FSWGWorldSnapshotNode& Node = Snapshot.Nodes[Candidates[CandidateIndex].Value];
		Placements.Add({ Snapshot.ObjectTemplateNames[(int32)Node.NameID], Node.Position, Node.Direction });
	}
	return Placements;
}

void FSWGPlanetMapScene::RequestBuildings(TArray<FBuildingPlacement>&& Placements)
{
	// One request per template; cities reuse a handful of building types.
	TMap<FString, TArray<FTransform>> TransformsByTemplate;
	for (const FBuildingPlacement& Placement : Placements)
	{
		// Same composition the streamed snapshot uses (USWGTerrainSubsystem::SpawnPendingSnapshotObjects).
		TransformsByTemplate.FindOrAdd(Placement.TemplatePath).Add(FTransform(Placement.Rotation, SWGToUnrealSpace(Placement.Position)));
	}
	const TWeakPtr<FSWGPlanetMapScene> WeakThis = AsShared();
	const int32 Generation = BuildingGeneration;
	for (TPair<FString, TArray<FTransform>>& Pair : TransformsByTemplate)
	{
		MeshGenerator->RequestTemplateStaticMesh(Pair.Key,
			[WeakThis, Generation, Transforms = MoveTemp(Pair.Value)](UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
			{
				const TSharedPtr<FSWGPlanetMapScene> Scene = WeakThis.Pin();
				if (!Scene || !Mesh || Scene->BuildingGeneration != Generation)
				{
					return;
				}
				for (const FTransform& Transform : Transforms)
				{
					UStaticMeshComponent* Building = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
					Building->SetMobility(EComponentMobility::Movable);
					Building->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Building->SetStaticMesh(Mesh);
					// 1-based; the last level is the low-poly silhouette the map wants.
					Building->SetForcedLodModel(Mesh->GetNumLODs());
					for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
					{
						Building->SetMaterial(MaterialIndex, Materials[MaterialIndex]);
					}
					Scene->PreviewScene.AddComponent(Building, Transform);
					Scene->BuildingComponents.Add(Building);
				}
				Scene->bDirty = true;
			});
	}
}

float FSWGPlanetMapScene::GetGroundHeight(const FVector2D& RawPoint) const
{
	if (Heights.Num() != GridSize * GridSize)
	{
		return 0.f;
	}
	const float HalfMap = MapSize * 0.5f;
	const float Column = FMath::Clamp(((RawPoint.X + HalfMap) / MapSize * ReliefSize - 0.5f) / GridStride, 0.f, GridSize - 1.f);
	const float Row = FMath::Clamp(((HalfMap - RawPoint.Y) / MapSize * ReliefSize - 0.5f) / GridStride, 0.f, GridSize - 1.f);
	const int32 Column0 = FMath::Min(FMath::FloorToInt(Column), GridSize - 2);
	const int32 Row0 = FMath::Min(FMath::FloorToInt(Row), GridSize - 2);
	const float ColumnAlpha = Column - Column0;
	const float RowAlpha = Row - Row0;
	const float North = FMath::Lerp(Heights[Row0 * GridSize + Column0], Heights[Row0 * GridSize + Column0 + 1], ColumnAlpha);
	const float South = FMath::Lerp(Heights[(Row0 + 1) * GridSize + Column0], Heights[(Row0 + 1) * GridSize + Column0 + 1], ColumnAlpha);
	return FMath::Lerp(North, South, RowAlpha);
}

void FSWGPlanetMapScene::SetViewportSize(const FIntPoint& Size)
{
	const FIntPoint Clamped(FMath::Clamp(Size.X, 64, 4096), FMath::Clamp(Size.Y, 64, 4096));
	if (Clamped != ViewportSize)
	{
		ViewportSize = Clamped;
		RenderTarget->ResizeTarget(ViewportSize.X, ViewportSize.Y);
		bDirty = true;
	}
}

void FSWGPlanetMapScene::SetCamera(const FSWGPlanetMapCamera& InCamera)
{
	if (!InCamera.IsNearlyEqual(Camera))
	{
		bDirty = true;
	}
	Camera = InCamera;
}

FRotator FSWGPlanetMapScene::EyeRotation() const
{
	return FRotator(Camera.GetPitch(), Camera.Yaw, 0.f);
}

FVector FSWGPlanetMapScene::EyeLocation() const
{
	const FVector Target = SWGToUnrealSpace(FVector(Camera.Target.X, Camera.Target.Y, GetGroundHeight(Camera.Target)));
	return Target - EyeRotation().Vector() * SWGToUnrealSpace(Camera.Distance);
}

float FSWGPlanetMapScene::FocalLengthPixels() const
{
	return ViewportSize.X * 0.5f / FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5f));
}

bool FSWGPlanetMapScene::Project(const FVector& RawPoint, FVector2D& OutPixel) const
{
	const FRotationMatrix Axes(EyeRotation());
	const FVector Relative = SWGToUnrealSpace(RawPoint) - EyeLocation();
	const float Forward = Relative | Axes.GetUnitAxis(EAxis::X);
	if (Forward <= Capture->CustomNearClippingPlane)
	{
		return false;
	}
	const float Focal = FocalLengthPixels();
	OutPixel.X = ViewportSize.X * 0.5f + (Relative | Axes.GetUnitAxis(EAxis::Y)) / Forward * Focal;
	OutPixel.Y = ViewportSize.Y * 0.5f - (Relative | Axes.GetUnitAxis(EAxis::Z)) / Forward * Focal;
	return true;
}

bool FSWGPlanetMapScene::Deproject(const FVector2D& Pixel, FVector2D& OutRawPoint) const
{
	const FRotationMatrix Axes(EyeRotation());
	const float Focal = FocalLengthPixels();
	const FVector Direction = Axes.GetUnitAxis(EAxis::X)
		+ Axes.GetUnitAxis(EAxis::Y) * ((Pixel.X - ViewportSize.X * 0.5f) / Focal)
		- Axes.GetUnitAxis(EAxis::Z) * ((Pixel.Y - ViewportSize.Y * 0.5f) / Focal);
	if (Direction.Z > -KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const FVector Eye = EyeLocation();
	const float PlaneHeight = SWGToUnrealSpace(GetGroundHeight(Camera.Target));
	const FVector Hit = Eye + Direction * ((PlaneHeight - Eye.Z) / Direction.Z);
	const FVector RawHit = SWGToRawSpace(Hit);
	OutRawPoint = FVector2D(RawHit.X, RawHit.Y);
	return true;
}

void FSWGPlanetMapScene::RenderIfDirty()
{
	check(IsInGameThread());
	if (bDirty)
	{
		RemainingSettleFrames = SettleFrames;
		bDirty = false;
	}
	else if (RemainingSettleFrames <= 0)
	{
		return;
	}
	else
	{
		--RemainingSettleFrames;
	}
	Capture->SetWorldLocationAndRotation(EyeLocation(), EyeRotation());
	Capture->CaptureScene();
}
