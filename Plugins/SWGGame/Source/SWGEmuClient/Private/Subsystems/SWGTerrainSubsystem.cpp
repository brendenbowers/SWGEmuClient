#include "Subsystems/SWGTerrainSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "Subsystems/SWGInteriorStreamingSubsystem.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGTerrainEvaluator.h"
#include "TRE/SWGWorldSnapshotReader.h"
#include "TRE/SWGTerrainModifier.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "TRE/SWGFormTagMapping.h"
#include "TRE/SWGDDSTextureLoader.h"
#include "TRE/SWGShaderReader.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeDataAccess.h"
#include "Engine/Texture2D.h"
#include "Engine/DataTable.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SceneComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "CompGeom/PolygonTriangulation.h"
#include "TRE/SWGColorRampReader.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	TAutoConsoleVariable<int32> CVarTerrainLoadRadius(
		TEXT("swg.TerrainLoadRadius"), 2,
		TEXT("Terrain tiles (512 m) kept loaded in every direction around the player — 2 is a 5x5 square, ground to 1-1.5 km out."));

	TAutoConsoleVariable<int32> CVarTerrainUnloadRadius(
		TEXT("swg.TerrainUnloadRadius"), 3,
		TEXT("Tiles further than this from the player are unloaded. Kept at least one past the load radius so a player at a tile edge doesn't thrash."));

	TAutoConsoleVariable<int32> CVarSnapshotLoadRadius(
		TEXT("swg.SnapshotLoadRadius"), 1,
		TEXT("Tiles (512 m) around the player whose .ws static objects are spawned — 1 is the 3x3, 512-1024 m out. Never wider than swg.TerrainLoadRadius."));

	TAutoConsoleVariable<int32> CVarSnapshotUnloadRadius(
		TEXT("swg.SnapshotUnloadRadius"), 2,
		TEXT("Tiles further than this have their static objects destroyed while the ground stays. Kept at least the load radius."));

	TAutoConsoleVariable<float> CVarSnapshotSpawnBudgetMs(
		TEXT("swg.SnapshotSpawnBudgetMs"), 2.0f,
		TEXT("Game-thread time per frame spent spawning a streamed tile's .ws objects. At least one spawns each frame regardless."));

	TAutoConsoleVariable<int32> CVarSWGLighting(
		TEXT("swg.SWGLighting"), 1,
		TEXT("Retail-style lighting: flat colour-ramp ambient everywhere with dynamic GI off. 0 keeps the project's Lumen setup."));

	TAutoConsoleVariable<float> CVarTimeOfDay(
		TEXT("swg.TimeOfDay"), 0.25f,
		TEXT("Position through the planet's day cycle (0..1) the colour ramp is sampled at — 0.25 is noon. Takes effect on the next zone load."));

	TAutoConsoleVariable<float> CVarAmbientIntensity(
		TEXT("swg.AmbientIntensity"), 1.5f,
		TEXT("Sky light intensity multiplying the colour ramp's ambient colour."));

	TAutoConsoleVariable<float> CVarSunIntensity(
		TEXT("swg.SunIntensity"), 1.0f,
		TEXT("Multiplier on the sun's ramp-derived intensity."));

	// Landscape's uint16 height packing represents a fixed +/-256 *local* height
	// range (LANDSCAPE_ZSCALE is hardcoded regardless of actor Z scale); the
	// actor's Z scale stretches that into final world units. Baked heights must
	// be pre-divided by this same constant before packing (see PackHeightMip) —
	// SpawnLandscapeActor uses it directly as the actor's Z scale.
	constexpr float HeightZScale = 8.0f; // +/-2048 world units of representable height
	// Terrain layers are detail textures, not a single decal for an entire
	// 2km baked tile. Raw world coordinates keep adjacent tiles in phase.
	constexpr float TerrainTextureRepeatWorldSize = 8.0f;

	// Interpolation across one base quad's corners, (U,V) running 0-1 from the
	// (Col,Row) corner — exactly the surface that quad's two triangles describe.
	float BilinearSample(float V00, float V10, float V01, float V11, double U, double V)
	{
		return (float)(V00 * (1.0 - U) * (1.0 - V) + V10 * U * (1.0 - V) + V01 * (1.0 - U) * V + V11 * U * V);
	}

	FVector3f BilinearSample(const FVector3f& V00, const FVector3f& V10, const FVector3f& V01, const FVector3f& V11, double U, double V)
	{
		return FVector3f(
			BilinearSample(V00.X, V10.X, V01.X, V11.X, U, V),
			BilinearSample(V00.Y, V10.Y, V01.Y, V11.Y, U, V),
			BilinearSample(V00.Z, V10.Z, V01.Z, V11.Z, U, V));
	}

	// Per-2x2-quad {Min,Max,Average} stats for one mip level — see
	// LandscapeComponent.h:503-512 (MipToMipMaxDeltas) for what these feed into.
	struct FSWGQuadHeightInfo
	{
		float Min = 0.0f;
		float Max = 0.0f;
		float Average = 0.0f;
	};

	// MipHeights[0] = the base Resolution x Resolution grid; MipHeights[L] is
	// (Resolution >> L) x (Resolution >> L), built via plain 2x2 box downsampling
	// (equivalent to bilinear at clean power-of-two boundaries). A full chain
	// down to 1x1 is required — confirmed via ULandscapeComponent::GetNumRelevantMips
	// (Landscape.cpp:2119-2127): a single mip fails check(NumRelevantMips > 0).
	void BuildMipHeightPyramid(const TArray<float>& BaseHeights, int32 Resolution, int32 NumMips, TArray<TArray<float>>& OutMipHeights)
	{
		OutMipHeights.SetNum(NumMips);
		OutMipHeights[0] = BaseHeights;

		for (int32 Mip = 1; Mip < NumMips; ++Mip)
		{
			const int32 SrcDim = Resolution >> (Mip - 1);
			const int32 DstDim = Resolution >> Mip;
			const TArray<float>& Src = OutMipHeights[Mip - 1];
			TArray<float>& Dst = OutMipHeights[Mip];
			Dst.SetNumUninitialized(DstDim * DstDim);

			for (int32 Y = 0; Y < DstDim; ++Y)
			{
				for (int32 X = 0; X < DstDim; ++X)
				{
					const int32 SX = X * 2, SY = Y * 2;
					const float H00 = Src[SY * SrcDim + SX];
					const float H10 = Src[SY * SrcDim + SX + 1];
					const float H01 = Src[(SY + 1) * SrcDim + SX];
					const float H11 = Src[(SY + 1) * SrcDim + SX + 1];
					Dst[Y * DstDim + X] = (H00 + H10 + H01 + H11) * 0.25f;
				}
			}
		}
	}

	// Non-overlapping 2x2 quad stats within one mip's own height grid — the same
	// grouping BuildMipHeightPyramid uses to produce the next mip down.
	void BuildQuadInfo(const TArray<float>& MipHeightGrid, int32 Dim, TArray<FSWGQuadHeightInfo>& OutQuads)
	{
		const int32 QuadDim = Dim / 2;
		OutQuads.SetNumUninitialized(QuadDim * QuadDim);

		for (int32 Y = 0; Y < QuadDim; ++Y)
		{
			for (int32 X = 0; X < QuadDim; ++X)
			{
				const int32 SX = X * 2, SY = Y * 2;
				const float H00 = MipHeightGrid[SY * Dim + SX];
				const float H10 = MipHeightGrid[SY * Dim + SX + 1];
				const float H01 = MipHeightGrid[(SY + 1) * Dim + SX];
				const float H11 = MipHeightGrid[(SY + 1) * Dim + SX + 1];

				FSWGQuadHeightInfo Info;
				Info.Min = FMath::Min(FMath::Min(H00, H10), FMath::Min(H01, H11));
				Info.Max = FMath::Max(FMath::Max(H00, H10), FMath::Max(H01, H11));
				Info.Average = (H00 + H10 + H01 + H11) * 0.25f;
				OutQuads[Y * QuadDim + X] = Info;
			}
		}
	}

	// Layout confirmed against UE::Landscape::Private::ComputeMipToMipMaxDeltas*
	// (LandscapeUtilsPrivate.cpp:33-63, not editor-gated): for NumRelevantMips
	// mips, mip M has (NumRelevantMips - 1 - M) entries (deltas to every mip
	// above it), laid out consecutively: mip 0's block, then mip 1's, etc.
	int32 CountForMip(int32 MipIndex, int32 NumRelevantMips)
	{
		return NumRelevantMips - 1 - MipIndex;
	}

	int32 OffsetForMip(int32 MipIndex, int32 NumRelevantMips)
	{
		int32 Offset = 0;
		for (int32 i = 0; i < MipIndex; ++i)
		{
			Offset += CountForMip(i, NumRelevantMips);
		}
		return Offset;
	}

	// Reimplements the editor-only delta algorithm (LandscapeEdit.cpp:172-329,
	// #if WITH_EDITOR — cannot be called directly, only replicated): for each
	// (SourceMip, DestMip) pair, the max delta is the worst-case gap between any
	// source quad's Min/Max and the average of the corresponding (coordinate-
	// halved) quad at DestMip.
	void ComputeMipToMipMaxDeltas(const TArray<TArray<float>>& MipHeights, int32 Resolution, int32 NumRelevantMips, TArray<double>& OutDeltas)
	{
		TArray<TArray<FSWGQuadHeightInfo>> MipQuads;
		MipQuads.SetNum(NumRelevantMips);
		for (int32 Mip = 0; Mip < NumRelevantMips; ++Mip)
		{
			BuildQuadInfo(MipHeights[Mip], Resolution >> Mip, MipQuads[Mip]);
		}

		const int32 TotalCount = OffsetForMip(NumRelevantMips - 1, NumRelevantMips) + CountForMip(NumRelevantMips - 1, NumRelevantMips);
		OutDeltas.SetNumZeroed(TotalCount);

		for (int32 SourceMip = 0; SourceMip < NumRelevantMips - 1; ++SourceMip)
		{
			const int32 SourceQuadDim = (Resolution >> SourceMip) / 2;
			const TArray<FSWGQuadHeightInfo>& SourceQuads = MipQuads[SourceMip];

			for (int32 DestMip = SourceMip + 1; DestMip < NumRelevantMips; ++DestMip)
			{
				const int32 Shift = DestMip - SourceMip;
				const int32 DestQuadDim = (Resolution >> DestMip) / 2;
				const TArray<FSWGQuadHeightInfo>& DestQuads = MipQuads[DestMip];

				double MaxDelta = 0.0;
				for (int32 QY = 0; QY < SourceQuadDim; ++QY)
				{
					for (int32 QX = 0; QX < SourceQuadDim; ++QX)
					{
						const int32 DQX = QX >> Shift;
						const int32 DQY = QY >> Shift;
						const FSWGQuadHeightInfo& SrcQ = SourceQuads[QY * SourceQuadDim + QX];
						const FSWGQuadHeightInfo& DstQ = DestQuads[DQY * DestQuadDim + DQX];

						const double D1 = FMath::Abs((double)SrcQ.Min - DstQ.Average);
						const double D2 = FMath::Abs((double)SrcQ.Max - DstQ.Average);
						MaxDelta = FMath::Max(MaxDelta, FMath::Max(D1, D2));
					}
				}

				const int32 Index = OffsetForMip(SourceMip, NumRelevantMips) + (DestMip - SourceMip - 1);
				OutDeltas[Index] = MaxDelta;
			}
		}
	}

	TArray<FColor> PackHeightMip(const TArray<float>& Heights, const FColor& DefaultNormal)
	{
		TArray<FColor> Packed;
		Packed.SetNumUninitialized(Heights.Num());
		for (int32 i = 0; i < Heights.Num(); ++i)
		{
			// GetTexHeight expects a *local* height (the fixed +/-256 range) —
			// divide the real-world baked height by the actor's Z scale to get
			// there, since GetTexHeight itself has no notion of our Z scale.
			FColor P = LandscapeDataAccess::PackHeight(LandscapeDataAccess::GetTexHeight(Heights[i] / HeightZScale));
			P.B = DefaultNormal.B;
			P.A = DefaultNormal.A;
			Packed[i] = P;
		}
		return Packed;
	}
}

void USWGTerrainSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	TreSubsystem = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));
	MeshGenerator = Cast<USWGMeshGeneratorSubsystem>(Collection.InitializeDependency(USWGMeshGeneratorSubsystem::StaticClass()));
}

void USWGTerrainSubsystem::Deinitialize()
{
	ResetZone();
}

void USWGTerrainSubsystem::BeginLoadTerrain(const FString TerrainVirtualPath, const FVector& SpawnPosition)
{
	check(IsInGameThread());

	// Everything from the previous planet goes now, not when the new data
	// lands: buildings for the new zone register holes and pads before the
	// .trn has parsed, and those must survive into the new zone.
	ResetZone();

	SpawnRawPosition = FVector2D(SpawnPosition.X, SpawnPosition.Y);
	ActiveTerrainVirtualPath = TerrainVirtualPath;

	Async(EAsyncExecution::Thread, [this, TerrainVirtualPath, SpawnPosition]()
		{
			LoadTerrain(TerrainVirtualPath, SpawnPosition);
		});
}

void USWGTerrainSubsystem::Error(const FString& ErrorMessage)
{
	if (!IsInGameThread())
	{
		AsyncTask(ENamedThreads::GameThread, [this, ErrorMessage]()
			{
				Error(ErrorMessage);
			});
		return;
	}
	else
	{
		OnTerrainError.Broadcast(ErrorMessage);
	}
}

void USWGTerrainSubsystem::LoadTerrain(const FString& TerrainVirtualPath, const FVector& SpawnPosition)
{
	UE_LOG(LogTemp, Verbose, TEXT("USWGTerrainSubsystem: Begin loading terrain: %s"), *TerrainVirtualPath);

	TSharedPtr<FSWGTerrainData, ESPMode::ThreadSafe> Parsed = MakeShared<FSWGTerrainData, ESPMode::ThreadSafe>();
	if (!ParseTerrain(TerrainVirtualPath, *Parsed))
	{
		Error(FString::Printf(TEXT("Failed to parse terrain: %s"), *TerrainVirtualPath));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: %s — map %.0f, chunk %.0f x %u tiles (pole spacing %.2f), global water %d @ %.1f, %d top-level layer(s)"),
		*TerrainVirtualPath, Parsed->Header.MapSize, Parsed->Header.ChunkSize, Parsed->Header.TilesPerChunk, Parsed->Header.GetPoleSpacing(),
		Parsed->Header.bUseGlobalWaterTable ? 1 : 0, Parsed->Header.GlobalWaterTableHeight, Parsed->TopLevelLayers.Num());

	TMap<FIntPoint, TArray<int32>> NodesByTile;
	TSharedPtr<const FSWGWorldSnapshotData, ESPMode::ThreadSafe> Snapshot = LoadWorldSnapshot(TerrainVirtualPath, NodesByTile);

	// Nothing here touched a UObject; the streaming itself is game-thread work.
	AsyncTask(ENamedThreads::GameThread, [this, TerrainVirtualPath, Parsed, Snapshot, NodesByTile = MoveTemp(NodesByTile)]() mutable
		{
			if (TerrainVirtualPath != ActiveTerrainVirtualPath)
			{
				// Zone changed again while this parsed; the newer load owns the subsystem now.
				return;
			}

			PlanetData = Parsed;
			SnapshotData = Snapshot;
			SnapshotNodesByTile = MoveTemp(NodesByTile);
			bTerrainDataCached = true;

			SetupPlanetLighting(TerrainVirtualPath);
			SpawnTerrainActor();
			SpawnWaterBodies();
			OnTerrainLoaded.Broadcast();

			// The tiles the player lands on; OnTerrainReady waits for exactly these.
			const FIntPoint SpawnTile = TileCoordAt(SpawnRawPosition);
			for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
			{
				for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
				{
					const FIntPoint Coord = SpawnTile + FIntPoint(OffsetX, OffsetY);
					if (IsTileOnMap(Coord))
					{
						InitialTiles.Add(Coord);
					}
				}
			}

			// Pads queued during the parse go in before the first bake so the
			// spawn tiles come out flattened rather than re-baked a moment later.
			FlushPendingObjectTerrainModifications();

			TimeUntilNextSweep = 0.0f;
			UpdateStreaming();
		});
}

void USWGTerrainSubsystem::SpawnTerrainActor()
{
	check(IsInGameThread());

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: no valid world to spawn terrain mesh in"));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* TerrainActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
	if (!TerrainActor)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to spawn terrain mesh actor"));
		return;
	}

	// A plain AActor has no root component at construction time. The actor sits
	// at the world origin; each tile component carries its own origin instead,
	// so no single grid origin has to be chosen up front.
	USceneComponent* TerrainRoot = NewObject<USceneComponent>(TerrainActor, TEXT("TerrainRoot"));
	TerrainActor->SetRootComponent(TerrainRoot);
	TerrainRoot->RegisterComponent();

	TerrainMeshActor = TerrainActor;
}

void USWGTerrainSubsystem::ResetZone()
{
	check(IsInGameThread());

	++TerrainGeneration;
	bTerrainDataCached = false;
	bInitialTilesReported = false;
	bHasLastStreamingCenter = false;
	OnZoneReset.Broadcast();

	// Emptied before the teardown: destroying a building removes its edits,
	// which would otherwise queue re-bakes for tiles that are about to go.
	TMap<FIntPoint, FSWGTerrainTile> OldTiles = MoveTemp(Tiles);
	Tiles.Reset();
	BakeQueue.Reset();
	for (TPair<FIntPoint, FSWGTerrainTile>& Pair : OldTiles)
	{
		DestroySnapshotActors(Pair.Value);
	}
	InitialTiles.Reset();
	// In-flight bakes still count until they land and see the new generation.

	if (IsValid(TerrainMeshActor))
	{
		TerrainMeshActor->Destroy();
		TerrainMeshActor = nullptr;
	}
	PooledTileComponents.Reset();
	if (IsValid(WaterActor))
	{
		WaterActor->Destroy();
		WaterActor = nullptr;
	}
	WaterMaterials.Reset();

	PlanetData.Reset();
	SnapshotData.Reset();
	SnapshotNodesByTile.Reset();

	OwnedEditLayers.Reset();
	PublishedEditLayers.Reset();
	TerrainHoles.Reset();
	StampedEditOwners.Reset();
	PendingObjectModifications.Reset();
	EditVersion = 0;
}

void USWGTerrainSubsystem::Tick(float DeltaTime)
{
	SpawnPendingSnapshotObjects();

	TimeUntilNextSweep -= DeltaTime;
	if (TimeUntilNextSweep > 0.0f)
	{
		return;
	}
	TimeUntilNextSweep = StreamingSweepInterval;
	UpdateStreaming();
}

TStatId USWGTerrainSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGTerrainSubsystem, STATGROUP_Tickables);
}

void USWGTerrainSubsystem::SetupPlanetLighting(const FString& TerrainVirtualPath)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Terrain reloads are allowed during zone travel. Remove only the actors we
	// created, leaving level-authored lighting untouched.
	static const FName PlanetLightingTag(TEXT("SWGPlanetLighting"));
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(PlanetLightingTag))
		{
			It->Destroy();
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ADirectionalLight* Sun = nullptr;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		Sun = *It;
		break;
	}
	if (!Sun)
	{
		Sun = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-38.0f, -35.0f, 0.0f), SpawnParams);
		if (Sun)
		{
			Sun->Tags.Add(PlanetLightingTag);
		}
	}
	// Retail's lighting is the planet's colour ramp sampled at the time of
	// day: a flat ambient that reaches everywhere (shade, interiors), a sun
	// colour, and a fog colour. See FSWGColorRamp for the row layout.
	const FString ZoneName = FPaths::GetBaseFilename(TerrainVirtualPath).ToLower();
	const float DayFraction = FMath::Frac(CVarTimeOfDay.GetValueOnGameThread());
	FSWGColorRamp Ramp;
	const bool bHasRamp = LoadPlanetColorRamp(ZoneName, Ramp);
	const FLinearColor AmbientColor = bHasRamp ? Ramp.Sample(FSWGColorRamp::Ambient, DayFraction) : FLinearColor(0.35f, 0.35f, 0.4f);
	const FLinearColor SunColor = bHasRamp ? Ramp.Sample(FSWGColorRamp::SunDiffuse, DayFraction) : FLinearColor::White;
	const FLinearColor FogColor = bHasRamp ? Ramp.Sample(FSWGColorRamp::Fog, DayFraction) : FLinearColor(0.60f, 0.72f, 0.78f);

	// The ramp's sun rises near column 16, peaks at 64 and sets by 128 —
	// elevation follows that arc, with a low dim light standing in for the
	// moon the rest of the cycle.
	const float DayArc = (DayFraction - 0.0625f) / 0.4375f;
	const float SunElevation = DayArc > 0.0f && DayArc < 1.0f ? FMath::Max(5.0f, 70.0f * FMath::Sin(DayArc * UE_PI)) : 5.0f;
	const float SunStrength = FMath::Max3(SunColor.R, SunColor.G, SunColor.B);

	if (Sun)
	{
		Sun->SetActorRotation(FRotator(-SunElevation, -35.0f, 0.0f));
		UDirectionalLightComponent* SunComponent = Sun->GetComponent();
		SunComponent->SetMobility(EComponentMobility::Movable);
		SunComponent->SetAtmosphereSunLight(true);
		SunComponent->SetUseTemperature(false);
		SunComponent->SetLightColor(SunStrength > 0.0f ? SunColor / SunStrength : FLinearColor::White);
		SunComponent->SetIntensity(3.0f * FMath::Max(SunStrength, 0.05f) * CVarSunIntensity.GetValueOnGameThread());
	}

	ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (SkyLight)
	{
		SkyLight->Tags.Add(PlanetLightingTag);
		USkyLightComponent* SkyLightComponent = SkyLight->GetLightComponent();
		SkyLightComponent->SetMobility(EComponentMobility::Movable);
		SkyLightComponent->SetRealTimeCaptureEnabled(true);
		// Tinted by the ramp's ambient; the captured sky supplies the shape.
		// No shadowing: retail's ambient was a constant term, not an
		// occluded one, which is what keeps shaded ground and rooms lit.
		SkyLightComponent->SetLightColor(AmbientColor);
		SkyLightComponent->SetIntensity(CVarAmbientIntensity.GetValueOnGameThread());
		SkyLightComponent->SetCastShadows(false);
		SkyLightComponent->bLowerHemisphereIsBlack = false;
		SkyLightComponent->SetLowerHemisphereColor(AmbientColor);
	}

	// Lumen would occlude that ambient indoors and in shade, so the SWG look
	// runs without dynamic GI (screen-space reflections stand in for Lumen's).
	if (CVarSWGLighting.GetValueOnGameThread() != 0)
	{
		if (IConsoleVariable* GIMethod = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod")))
		{
			GIMethod->Set(0, ECVF_SetByGameSetting);
		}
		if (IConsoleVariable* ReflectionMethod = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod")))
		{
			ReflectionMethod->Set(2, ECVF_SetByGameSetting);
		}
	}

	// A SkyAtmosphere is the UE equivalent of SWG's gradient-sky backdrop. The
	// selected Naboo texture is retained as the planet's data source while the
	// original effect's proprietary shader is still being ported.
	AActor* AtmosphereActor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (AtmosphereActor)
	{
		AtmosphereActor->Tags.Add(PlanetLightingTag);
		USceneComponent* Root = NewObject<USceneComponent>(AtmosphereActor, TEXT("SWGSkyRoot"));
		AtmosphereActor->SetRootComponent(Root);
		Root->RegisterComponent();
		USkyAtmosphereComponent* Atmosphere = NewObject<USkyAtmosphereComponent>(AtmosphereActor, TEXT("SWGSkyAtmosphere"));
		Atmosphere->SetupAttachment(Root);
		Atmosphere->RegisterComponent();
	}

	AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (Fog)
	{
		Fog->Tags.Add(PlanetLightingTag);
		Fog->GetComponent()->SetFogDensity(0.0015f);
		Fog->GetComponent()->SetFogInscatteringColor(FogColor);
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: %s lighting at day %.2f from %s — ambient (%.2f, %.2f, %.2f), sun (%.2f, %.2f, %.2f) elevation %.0f, fog (%.2f, %.2f, %.2f)"),
		*ZoneName, DayFraction, bHasRamp ? TEXT("colour ramp") : TEXT("defaults (no ramp)"),
		AmbientColor.R, AmbientColor.G, AmbientColor.B, SunColor.R, SunColor.G, SunColor.B, SunElevation, FogColor.R, FogColor.G, FogColor.B);
}

bool USWGTerrainSubsystem::LoadPlanetColorRamp(const FString& ZoneName, FSWGColorRamp& OutRamp) const
{
	if (!TreSubsystem)
	{
		return false;
	}

	// The .trn's EGRP names environments ("global", "mtns", "forests"...);
	// each maps to terrain/colorramp/<planet>_<env>0.tga. Only the planet-wide
	// "global" ramp is used for now — per-region environments (the AENV
	// affector) would blend between them.
	const TArray<FString> Candidates = {
		FString::Printf(TEXT("terrain/colorramp/%s_global0.tga"), *ZoneName),
		FString::Printf(TEXT("terrain/colorramp/%s_%s_global0.tga"), *ZoneName, *ZoneName),
		TEXT("terrain/colorramp/09_default0.tga"),
	};
	for (const FString& Path : Candidates)
	{
		if (TreSubsystem->FileExists(Path) && FSWGColorRampReader::ReadTga(TreSubsystem->ExtractFile(Path), OutRamp))
		{
			UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: using colour ramp %s"), *Path);
			return true;
		}
	}
	return false;
}

TSharedPtr<const FSWGWorldSnapshotData, ESPMode::ThreadSafe> USWGTerrainSubsystem::LoadWorldSnapshot(const FString& TerrainVirtualPath, TMap<FIntPoint, TArray<int32>>& OutNodesByTile)
{
	OutNodesByTile.Reset();

	// "terrain/tatooine.trn" -> "tatooine" -> "snapshot/tatooine.ws".
	const FString ZoneName = FPaths::GetBaseFilename(TerrainVirtualPath);
	const FString SnapshotPath = FString::Printf(TEXT("snapshot/%s.ws"), *ZoneName);

	FSWGIffReader SnapshotReader = TreSubsystem->CreateIffReader(SnapshotPath);
	if (!SnapshotReader.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: no world snapshot found at %s — no static world objects will spawn"), *SnapshotPath);
		return nullptr;
	}

	TSharedPtr<FSWGWorldSnapshotData, ESPMode::ThreadSafe> Snapshot = MakeShared<FSWGWorldSnapshotData, ESPMode::ThreadSafe>();
	if (!FSWGWorldSnapshotReader::ReadWorldSnapshot(SnapshotReader, *Snapshot))
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to parse world snapshot %s"), *SnapshotPath);
		return nullptr;
	}

	if (!FormTagMappingTable)
	{
		// Loadable off the game thread — SWGInitializationState's own CRC map
		// generation already does exactly this from a background ThreadPool task.
		FormTagMappingTable = LoadObject<UDataTable>(nullptr,
			TEXT("/Game/SWGEmu/Data/DT_SWGFormTagMappings.DT_SWGFormTagMappings"));
	}

	for (int32 NodeIndex = 0; NodeIndex < Snapshot->Nodes.Num(); ++NodeIndex)
	{
		const FSWGWorldSnapshotNode& Node = Snapshot->Nodes[NodeIndex];
		OutNodesByTile.FindOrAdd(TileCoordAt(FVector2D(Node.Position.X, Node.Position.Y))).Add(NodeIndex);
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: world snapshot %s — %d top-level node(s) across %d tile(s)"),
		*SnapshotPath, Snapshot->Nodes.Num(), OutNodesByTile.Num());

	return Snapshot;
}

TArray<FSWGWorldSnapshotSpawnInfo> USWGTerrainSubsystem::ResolveSnapshotObjectsForTile(const FIntPoint& Coord) const
{
	TArray<FSWGWorldSnapshotSpawnInfo> Result;

	const TArray<int32>* NodeIndices = SnapshotNodesByTile.Find(Coord);
	if (!SnapshotData.IsValid() || !NodeIndices)
	{
		return Result;
	}

	for (const int32 NodeIndex : *NodeIndices)
	{
		FSWGWorldSnapshotSpawnInfo Info;
		if (ResolveWorldSnapshotNode(SnapshotData->Nodes[NodeIndex], *SnapshotData, Info))
		{
			Result.Add(MoveTemp(Info));
		}
	}

	return Result;
}

bool USWGTerrainSubsystem::ResolveWorldSnapshotNode(const FSWGWorldSnapshotNode& Node, const FSWGWorldSnapshotData& Snapshot, FSWGWorldSnapshotSpawnInfo& OutInfo) const
{
	if (!Snapshot.ObjectTemplateNames.IsValidIndex((int32)Node.NameID))
		return false;

	const FString& TemplateName = Snapshot.ObjectTemplateNames[(int32)Node.NameID];

	FSWGIffReader TemplateReader = TreSubsystem->CreateIffReader(TemplateName);
	if (!TemplateReader.IsValid())
		return false;

	const FName FormType = TemplateReader.GetRootFormType();
	if (FormType == NAME_None || !FormTagMappingTable)
		return false;

	const FSWGFormTagMapping* Mapping = FormTagMappingTable->FindRow<FSWGFormTagMapping>(FormType, TEXT("USWGTerrainSubsystem"), false);
	if (!Mapping || !Mapping->ActorClass)
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGTerrainSubsystem: .ws node %u template %s (form %s) has no actor class"), Node.ObjectID, *TemplateName, *FormType.ToString());
		return false;
	}

	OutInfo.ObjectId = (int64)Node.ObjectID;
	OutInfo.CellNumber = (int32)Node.CellID;
	OutInfo.ActorClass = Mapping->ActorClass;
	OutInfo.Position = Node.Position;
	OutInfo.Rotation = Node.Direction;
	OutInfo.TemplateName = TemplateName;

	for (const FSWGWorldSnapshotNode& ChildNode : Node.Children)
	{
		FSWGWorldSnapshotSpawnInfo ChildInfo;
		if (ResolveWorldSnapshotNode(ChildNode, Snapshot, ChildInfo))
		{
			OutInfo.Children.Add(MoveTemp(ChildInfo));
		}
	}

	return true;
}

void USWGTerrainSubsystem::QueueSnapshotObjectsForTile(const FIntPoint& Coord, FSWGTerrainTile& Tile, TArray<FSWGWorldSnapshotSpawnInfo>&& Objects)
{
	check(IsInGameThread());

	Tile.bSnapshotSpawned = true;
	Tile.PendingSnapshotObjects = MoveTemp(Objects);
	Tile.NextSnapshotIndex = 0;

	if (!Tile.PendingSnapshotObjects.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: tile (%d,%d) queued %d world snapshot object(s)"), Coord.X, Coord.Y, Tile.PendingSnapshotObjects.Num());
	}
}

void USWGTerrainSubsystem::SpawnPendingSnapshotObjects()
{
	check(IsInGameThread());

	UWorld* World = GetWorld();
	if (!World)
		return;

	// Nearest tile first, so what the player can see fills in before the horizon.
	FVector2D Center;
	GetStreamingCenter(Center);
	const FIntPoint CenterTile = TileCoordAt(Center);

	TArray<FIntPoint> WithPending;
	for (const TPair<FIntPoint, FSWGTerrainTile>& Pair : Tiles)
	{
		if (Pair.Value.NextSnapshotIndex < Pair.Value.PendingSnapshotObjects.Num())
		{
			WithPending.Add(Pair.Key);
		}
	}
	if (WithPending.IsEmpty())
		return;

	WithPending.Sort([&CenterTile](const FIntPoint& Left, const FIntPoint& Right)
		{
			const FIntPoint LeftDelta = Left - CenterTile;
			const FIntPoint RightDelta = Right - CenterTile;
			return LeftDelta.X * LeftDelta.X + LeftDelta.Y * LeftDelta.Y < RightDelta.X * RightDelta.X + RightDelta.Y * RightDelta.Y;
		});

	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;

	const double BudgetSeconds = FMath::Max(0.0f, CVarSnapshotSpawnBudgetMs.GetValueOnGameThread()) * 0.001;
	const double StartTime = FPlatformTime::Seconds();
	int32 SpawnedThisFrame = 0;

	for (const FIntPoint& Coord : WithPending)
	{
		FSWGTerrainTile& Tile = Tiles[Coord];

		// At least one per frame regardless of budget, so a slow frame can't
		// stall the queue outright.
		while (Tile.NextSnapshotIndex < Tile.PendingSnapshotObjects.Num()
			&& (SpawnedThisFrame == 0 || FPlatformTime::Seconds() - StartTime < BudgetSeconds))
		{
			const FSWGWorldSnapshotSpawnInfo& Info = Tile.PendingSnapshotObjects[Tile.NextSnapshotIndex++];
			// Info.Position is raw/native space straight from the .ws — scale to
			// final UE space right at this actor-placement boundary.
			SpawnWorldSnapshotNode(Info, FTransform(Info.Rotation, SWGToUnrealSpace(Info.Position)), nullptr, ObjectGraph, false, &Tile.SnapshotActors);
			++SpawnedThisFrame;
		}

		if (Tile.NextSnapshotIndex >= Tile.PendingSnapshotObjects.Num())
		{
			UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: tile (%d,%d) spawned %d world snapshot object(s), %d actor(s)"),
				Coord.X, Coord.Y, Tile.PendingSnapshotObjects.Num(), Tile.SnapshotActors.Num());
			Tile.PendingSnapshotObjects.Empty();
			Tile.NextSnapshotIndex = 0;
		}

		if (FPlatformTime::Seconds() - StartTime >= BudgetSeconds)
		{
			break;
		}
	}
}

void USWGTerrainSubsystem::DestroySnapshotActors(FSWGTerrainTile& Tile)
{
	check(IsInGameThread());

	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;

	for (const TWeakObjectPtr<AActor>& ActorWeak : Tile.SnapshotActors)
	{
		AActor* Actor = ActorWeak.Get();
		if (!Actor)
			continue;

		// Rooms the interior streamer opened are the building's; it takes them
		// down (and unregisters them) before the building itself goes.
		if (ASWGBuilding* Building = Cast<ASWGBuilding>(Actor))
		{
			Building->UnloadAllRooms();
		}

		if (const ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(Actor))
		{
			const int64 ObjectId = NetObject->GetObjectId();
			if (ObjectGraph)
			{
				ObjectGraph->UnregisterStaticObject(ObjectId);
			}
			RemoveObjectTerrainEdits(ObjectId);
		}

		Actor->Destroy();
	}

	Tile.SnapshotActors.Reset();
	Tile.PendingSnapshotObjects.Empty();
	Tile.NextSnapshotIndex = 0;
	Tile.bSnapshotSpawned = false;
}

AActor* USWGTerrainSubsystem::SpawnWorldSnapshotNode(const FSWGWorldSnapshotSpawnInfo& Info, const FTransform& WorldTransform, AActor* Parent, USWGObjectGraphSubsystem* ObjectGraph, bool bForceInterior, TArray<TWeakObjectPtr<AActor>>* OutSpawned)
{
	UWorld* World = GetWorld();
	if (!World)
		return nullptr;

	ASWGBuilding* ParentBuilding = Cast<ASWGBuilding>(Parent);
	const bool bIsCell = Info.ActorClass->IsChildOf(ASWGCell::StaticClass());

	// Without a POB there is nothing to finish a room against; spawning it
	// anyway leaves an actor at its building-relative origin, i.e. the world's.
	if (bIsCell && ParentBuilding && ParentBuilding->PortalData.Cells.IsEmpty())
	{
		return nullptr;
	}

	// Every room (and everything inside it) is handed to the building here
	// without creating an actor; USWGInteriorStreamingSubsystem brings it back
	// through this same function once the player is close enough / looking.
	if (bIsCell && !bForceInterior && ParentBuilding)
	{
		ParentBuilding->DeferredSnapshotCells.Add(Info);
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (USWGInteriorStreamingSubsystem* Streaming = GameInstance->GetSubsystem<USWGInteriorStreamingSubsystem>())
			{
				Streaming->RegisterBuilding(ParentBuilding);
			}
		}
		return nullptr;
	}

	// A cell spawns building-relative (identity, like the network path's
	// (0,0,0) SceneCreateObjectByCrc): FSWGCellSpawnHandler::FinishCell attaches
	// it with KeepRelativeTransform, which would double the building's
	// transform if the cell already sat at its world placement.
	const FTransform SpawnTransform = bIsCell ? FTransform(Info.Rotation, SWGToUnrealSpace(Info.Position)) : WorldTransform;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Actor = World->SpawnActor<AActor>(Info.ActorClass, SpawnTransform, SpawnParams);
	if (!Actor)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: failed to spawn world snapshot object %s"), *Info.TemplateName);
		return nullptr;
	}

	if (OutSpawned)
	{
		OutSpawned->Add(Actor);
	}

	if (ASWGBuilding* Building = Cast<ASWGBuilding>(Actor))
	{
		Building->SnapshotTransform = WorldTransform;
	}

	// The server never sends SceneCreateObjectByCrc for anything in the .ws
	// (Core3 BuildingObjectImplementation::sendTo: "static in the client"), but
	// it does reference these ids — NPCs are contained in a static cell, a
	// terminal is targeted — so the graph must know them.
	const ISWGNetworkObjectInterface* ParentObject = Cast<ISWGNetworkObjectInterface>(Parent);
	const int64 ParentObjectId = ParentObject ? ParentObject->GetObjectId() : 0;
	ASWGCell* Cell = Cast<ASWGCell>(Actor);

	// A cell registers only once FinishCell has attached it to its building:
	// registering re-applies containment for whatever is already in the room,
	// composing it against the cell's transform — which until then is the
	// building-relative spawn transform, i.e. the world origin.
	if (ObjectGraph && !Cell)
	{
		ObjectGraph->RegisterStaticObject(Info.ObjectId, Actor, ParentObjectId);
	}

	if (ASWGCell* ParentCell = Cast<ASWGCell>(Parent))
	{
		ParentCell->InteriorActors.Add(Actor);
	}

	if (Cell && !ParentBuilding)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: world snapshot cell %lld has no building parent — left unfinished"), Info.ObjectId);
		return Actor;
	}

	if (Cell)
	{
		// Same as the network path's UpdateContainment + TLCS baseline, only
		// both facts come from the .ws node at once.
		if (ObjectGraph)
		{
			ObjectGraph->SetCellNumber(Info.ObjectId, Info.CellNumber);
		}
		FSWGCellSpawnHandler::FinishCell(Cell, ParentBuilding, Info.CellNumber, TreSubsystem, MeshGenerator, bForceInterior);

		if (ObjectGraph)
		{
			ObjectGraph->RegisterStaticObject(Info.ObjectId, Actor, ParentObjectId);
		}
	}
	else if (MeshGenerator)
	{
		// A registered handler (buildings — see FSWGActorSpawnHandlerRegistry)
		// gets first refusal on continuing this actor's generation; only fall
		// back to the generic one-mesh-component path when nothing is
		// registered for its class.
		FSWGActorSpawnArguments SpawnInfo { 0, Info.ActorClass, Info.TemplateName };
		if (!FSWGActorSpawnHandlerRegistry::Get().TryHandle(*Actor, SpawnInfo))
		{
			MeshGenerator->RequestMeshForTemplatePath(Actor, Info.TemplateName);
		}
	}

	for (const FSWGWorldSnapshotSpawnInfo& Child : Info.Children)
	{
		// Composed into world space and left unattached: nothing in the .ws
		// ever moves, and both the building's and the cell's root components
		// are replaced once their meshes finish building, which would orphan
		// anything attached before then.
		const FTransform ChildRelative(Child.Rotation, SWGToUnrealSpace(Child.Position));
		SpawnWorldSnapshotNode(Child, ChildRelative * WorldTransform, Actor, ObjectGraph, bForceInterior, OutSpawned);
	}

	return Actor;
}

float USWGTerrainSubsystem::GetHeightAt(float X, float Y) const
{
	if (!bTerrainDataCached || !PlanetData.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: GetHeightAt called before any terrain has parsed"));
		return 0.0f;
	}
	const TArrayView<const FSWGTerrainLayer> Edits = PublishedEditLayers.IsValid() ? MakeArrayView(*PublishedEditLayers) : TArrayView<const FSWGTerrainLayer>();
	return FSWGTerrainEvaluator::GetHeight(*PlanetData, X, Y, Edits);
}

namespace
{
	/**
	 * Reads one string-valued XXXX field out of a shared object template,
	 * following the FORM DERV base-template chain when the field is present but
	 * flagged undefined (i.e. inherited). Payload layout is
	 * "<key>\0<definedFlag><value>\0"; a DERV's own XXXX is just a bare path
	 * with no flag byte, which never collides with a real field name.
	 */
	bool FindTemplateStringField(USWGTreSubsystem* TreSubsystem, const FString& TemplatePath, const FString& Key, FString& OutValue, int32 Depth = 0)
	{
		// Retail chains are 2-3 deep; this only guards against a malformed cycle.
		if (!TreSubsystem || Depth > 8)
		{
			return false;
		}

		FSWGIffReader Reader = TreSubsystem->CreateIffReader(TemplatePath);
		if (!Reader.IsValid())
		{
			return false;
		}

		const FString KeyUtf8 = Key;
		FString DervParent;
		bool bFound = false;

		TFunction<void(const FSWGIffChunk&)> Visit = [&](const FSWGIffChunk& Chunk)
		{
			if (bFound)
			{
				return;
			}

			if (Chunk.IsForm())
			{
				const bool bIsDerv = Chunk.FormType == SWG_IFF_TAG('D','E','R','V');
				for (const FSWGIffChunk& Child : Reader.ReadChildren(Chunk))
				{
					if (bIsDerv && Child.Tag == SWG_IFF_TAG('X','X','X','X') && DervParent.IsEmpty())
					{
						const uint8* D = Reader.GetChunkData(Child);
						const int32 Size = Reader.GetChunkSize(Child);
						int32 End = 0;
						while (End < Size && D[End] != 0) { ++End; }
						DervParent = FString::ConstructFromPtrSize((const ANSICHAR*)D, End);
						continue;
					}
					Visit(Child);
				}
				return;
			}

			if (Chunk.Tag != SWG_IFF_TAG('X','X','X','X'))
			{
				return;
			}

			const uint8* D = Reader.GetChunkData(Chunk);
			const int32 Size = Reader.GetChunkSize(Chunk);

			int32 KeyEnd = 0;
			while (KeyEnd < Size && D[KeyEnd] != 0) { ++KeyEnd; }
			if (KeyEnd >= Size)
			{
				return;
			}

			const FString ChunkKey = FString::ConstructFromPtrSize((const ANSICHAR*)D, KeyEnd);
			if (ChunkKey != KeyUtf8)
			{
				return;
			}

			const int32 FlagOffset = KeyEnd + 1;
			if (FlagOffset >= Size || D[FlagOffset] == 0)
			{
				// Present but undefined — the value comes from the DERV parent.
				return;
			}

			int32 ValueStart = FlagOffset + 1;
			int32 ValueEnd = ValueStart;
			while (ValueEnd < Size && D[ValueEnd] != 0) { ++ValueEnd; }

			OutValue = FString::ConstructFromPtrSize((const ANSICHAR*)(D + ValueStart), ValueEnd - ValueStart);
			bFound = true;
		};

		for (const FSWGIffChunk& Top : Reader.ReadChunks())
		{
			Visit(Top);
		}

		if (bFound)
		{
			return !OutValue.IsEmpty();
		}

		if (!DervParent.IsEmpty())
		{
			return FindTemplateStringField(TreSubsystem, DervParent, Key, OutValue, Depth + 1);
		}

		return false;
	}
}

bool USWGTerrainSubsystem::BuildObjectTerrainLayers(const FString& TemplatePath, const FVector& WorldPosition, float YawRadians, TArray<FSWGTerrainLayer>& OutLayers)
{
	OutLayers.Reset();

	FSWGTerrainPlacement Placement;
	Placement.WorldCenter = FVector2D(WorldPosition.X, WorldPosition.Y);
	Placement.YawRadians = YawRadians;
	Placement.BaseHeight = (float)WorldPosition.Z;

	// Retail's own order of preference: a full .lay layer graph wins over the
	// coarser footprint grid when a template carries both.
	FString LayPath;
	if (FindTemplateStringField(TreSubsystem, TemplatePath, TEXT("terrainModificationFileName"), LayPath))
	{
		FSWGIffReader LayReader = TreSubsystem->CreateIffReader(LayPath);
		FSWGTerrainData LayData;
		if (LayReader.IsValid() && FSWGTerrainReader::ReadLayerFile(LayReader, LayData))
		{
			for (FSWGTerrainLayer& Layer : LayData.TopLevelLayers)
			{
				FSWGTerrainModifier::TransformLayer(Layer, Placement);
				OutLayers.Add(MoveTemp(Layer));
			}

			if (OutLayers.Num() > 0)
			{
				return true;
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: %s references terrain modification %s, which failed to parse"), *TemplatePath, *LayPath);
	}

	// No .sfp fallback: a structure footprint does not shape terrain. Flattening
	// to one put the ground 1.46 above retail's at the Naboo cloning facility,
	// where retail matches unmodified terrain to within 0.15 — and Core3 reads
	// footprints for placement validation, not shaping.
	return false;
}

FBox2D USWGTerrainSubsystem::GetTileBounds(const FSWGBakedHeightmap& Heightmap)
{
	const float Extent = (HeightmapResolution - 1) * Heightmap.Spacing;
	const FVector2D Min(Heightmap.Origin.X, Heightmap.Origin.Y);
	return FBox2D(Min, Min + FVector2D(Extent, Extent));
}

void USWGTerrainSubsystem::FlushPendingObjectTerrainModifications()
{
	check(IsInGameThread());

	if (PendingObjectModifications.IsEmpty())
	{
		return;
	}

	// Move first: each replayed call re-enters ApplyObjectTerrainModification,
	// which would otherwise see the entry it is currently processing.
	TArray<FSWGPendingTerrainModification> Replaying = MoveTemp(PendingObjectModifications);
	PendingObjectModifications.Reset();

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: replaying %d terrain modification(s) deferred during terrain load"), Replaying.Num());

	for (const FSWGPendingTerrainModification& Pending : Replaying)
	{
		ApplyObjectTerrainModification(Pending.TemplatePath, Pending.WorldPosition, Pending.YawRadians, Pending.OwnerObjectId);
	}
}

bool USWGTerrainSubsystem::ApplyObjectTerrainModification(const FString& TemplatePath, const FVector& WorldPosition, float YawRadians, int64 OwnerObjectId)
{
	check(IsInGameThread());

	if (!bTerrainDataCached)
	{
		// Buildings reliably arrive before the async .trn parse completes, so
		// this is the normal path on zone entry, not an edge case. Queue rather
		// than drop — dropping meant a building's pad simply never applied.
		PendingObjectModifications.Add({ TemplatePath, WorldPosition, YawRadians, OwnerObjectId });
		UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: ApplyObjectTerrainModification(%s) before terrain finished loading — deferred (%d queued)"),
			*TemplatePath, PendingObjectModifications.Num());
		return true;
	}

	if (OwnerObjectId != 0 && StampedEditOwners.Contains(OwnerObjectId))
	{
		return true;
	}

	TArray<FSWGTerrainLayer> Layers;
	if (!BuildObjectTerrainLayers(TemplatePath, WorldPosition, YawRadians, Layers))
	{
		return false;
	}

	const float CentreBefore = GetHeightAt((float)WorldPosition.X, (float)WorldPosition.Y);

	FBox2D Affected(ForceInit);
	for (FSWGTerrainLayer& Layer : Layers)
	{
		FBox2D LayerBounds;
		if (!FSWGTerrainModifier::GetLayerWorldBounds(Layer, LayerBounds))
		{
			// An unbounded layer would apply everywhere. Nothing in retail's
			// .lay data is unbounded, so treat it as a data problem rather
			// than silently re-baking every loaded tile.
			UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: terrain layer '%s' has no bounded region — skipping it"), *Layer.Name);
			continue;
		}
		Affected += LayerBounds.Min;
		Affected += LayerBounds.Max;
		OwnedEditLayers.Add({ OwnerObjectId, MoveTemp(Layer) });
	}

	if (OwnerObjectId != 0)
	{
		StampedEditOwners.Add(OwnerObjectId);
	}

	PublishEditLayers();
	if (Affected.bIsValid)
	{
		InvalidateTilesOverlapping(Affected);
	}

	const float CentreAfter = GetHeightAt((float)WorldPosition.X, (float)WorldPosition.Y);
	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: terrain pad %s at (%.1f, %.1f) — centre height %.2f -> %.2f, %d edit layer(s) live"),
		*TemplatePath, WorldPosition.X, WorldPosition.Y, CentreBefore, CentreAfter, OwnedEditLayers.Num());

	return true;
}

void USWGTerrainSubsystem::AddTerrainHoles(const TArray<FSWGTerrainHole>& Holes)
{
	check(IsInGameThread());

	if (Holes.IsEmpty())
	{
		return;
	}

	FBox2D Affected(ForceInit);
	for (const FSWGTerrainHole& Hole : Holes)
	{
		const FBox2D Bounds = Hole.GetWorldBounds();
		Affected += Bounds.Min;
		Affected += Bounds.Max;
	}

	TerrainHoles.Append(Holes);

	// Holes are captured by copy per bake, so no publish step — just a version
	// bump so tiles already baking come back for another pass.
	++EditVersion;
	InvalidateTilesOverlapping(Affected);
}

void USWGTerrainSubsystem::RemoveObjectTerrainEdits(int64 OwnerObjectId)
{
	check(IsInGameThread());

	if (OwnerObjectId == 0)
	{
		return;
	}

	FBox2D Affected(ForceInit);
	bool bRemovedLayers = false;

	for (auto It = OwnedEditLayers.CreateIterator(); It; ++It)
	{
		if (It->OwnerObjectId != OwnerObjectId)
			continue;

		FBox2D LayerBounds;
		if (FSWGTerrainModifier::GetLayerWorldBounds(It->Layer, LayerBounds))
		{
			Affected += LayerBounds.Min;
			Affected += LayerBounds.Max;
		}
		It.RemoveCurrent();
		bRemovedLayers = true;
	}

	for (auto It = TerrainHoles.CreateIterator(); It; ++It)
	{
		if (It->OwnerObjectId != OwnerObjectId)
			continue;

		const FBox2D Bounds = It->GetWorldBounds();
		Affected += Bounds.Min;
		Affected += Bounds.Max;
		It.RemoveCurrent();
	}

	StampedEditOwners.Remove(OwnerObjectId);

	if (!Affected.bIsValid)
	{
		return;
	}

	if (bRemovedLayers)
	{
		PublishEditLayers();
	}
	else
	{
		++EditVersion;
	}
	InvalidateTilesOverlapping(Affected);
}

void USWGTerrainSubsystem::PublishEditLayers()
{
	check(IsInGameThread());

	TSharedPtr<TArray<FSWGTerrainLayer>, ESPMode::ThreadSafe> Published = MakeShared<TArray<FSWGTerrainLayer>, ESPMode::ThreadSafe>();
	Published->Reserve(OwnedEditLayers.Num());
	for (const FSWGOwnedTerrainLayer& Owned : OwnedEditLayers)
	{
		Published->Add(Owned.Layer);
	}

	PublishedEditLayers = Published;
	++EditVersion;
}

void USWGTerrainSubsystem::InvalidateTilesOverlapping(const FBox2D& Bounds)
{
	check(IsInGameThread());

	for (TPair<FIntPoint, FSWGTerrainTile>& Pair : Tiles)
	{
		if (!TileBounds(Pair.Key).Intersect(Bounds))
			continue;

		Pair.Value.WantedEditVersion = EditVersion;
		if (!Pair.Value.bBakeInFlight)
		{
			BakeQueue.Add(Pair.Key);
		}
		// A bake already running carries an older version; OnTileBakeFinished
		// sees the mismatch and re-queues it.
	}

	PumpBakeQueue();
	OnTerrainEditsChanged.Broadcast(Bounds);
}

bool USWGTerrainSubsystem::FSWGTerrainBakeSource::IsInHole(const FVector2D& RawPosition) const
{
	for (const FSWGTerrainHole& Hole : Holes)
	{
		if (Hole.Contains(RawPosition))
		{
			return true;
		}
	}
	return false;
}

USWGTerrainSubsystem::FSWGTerrainBakeSource USWGTerrainSubsystem::MakeBakeSource() const
{
	FSWGTerrainBakeSource Source;
	Source.Planet = PlanetData;
	Source.EditLayers = PublishedEditLayers;
	Source.Holes = TerrainHoles;
	Source.EditVersion = EditVersion;
	return Source;
}

bool USWGTerrainSubsystem::ParseTerrain(const FString& TerrainVirtualPath, FSWGTerrainData& OutTerrainData)
{
	FSWGIffReader Reader = TreSubsystem->CreateIffReader(TerrainVirtualPath);
	if (!Reader.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: Failed to create IFF reader for terrain: %s"), *TerrainVirtualPath);
		return false;
	}

	if (!FSWGTerrainReader::ReadTerrain(Reader, OutTerrainData))
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: Failed to read terrain data from IFF: %s"), *TerrainVirtualPath);
		return false;
	}

	return true;
}

FSWGBakedHeightmap USWGTerrainSubsystem::BakeHeightmap(const FSWGTerrainBakeSource& Source, const FVector& RegionOrigin) const
{
	const int32 Resolution = HeightmapResolution;
	const float Spacing = HeightmapWorldExtent / (Resolution - 1);
	const TArrayView<const FSWGTerrainLayer> Edits = Source.EditLayers.IsValid() ? MakeArrayView(*Source.EditLayers) : TArrayView<const FSWGTerrainLayer>();

	FSWGBakedHeightmap Heightmap;
	Heightmap.Origin = RegionOrigin;
	Heightmap.Spacing = Spacing;
	Heightmap.Heights.SetNumUninitialized(Resolution * Resolution);

	// Tracks min/max/out-of-range height values from this component's evaluator
	// calls — catches GetHeight diverging to huge values, which GetTexHeight's
	// Clamp would otherwise silently flatten to the +/-2048 ceiling/floor.
	float MinHeight = TNumericLimits<float>::Max();
	float MaxHeight = TNumericLimits<float>::Lowest();
	int32 OutOfRangeCount = 0;
	constexpr float RepresentableHeightLimit = 2048.0f;

	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		const float WorldY = RegionOrigin.Y + Row * Spacing;

		for (int32 Col = 0; Col < Resolution; ++Col)
		{
			const float WorldX = RegionOrigin.X + Col * Spacing;
			float Height = FSWGTerrainEvaluator::GetHeight(*Source.Planet, WorldX, WorldY, Edits);
			// Safety net: a NaN/Inf height here bakes straight into the landscape's
			// uint16 heightmap and comes out as an extreme spike (this is what was
			// actually happening — see FSWGMapFractal::GetNoise's Pow() fix for the
			// specific cause found). Guard at the source too, since other affector
			// math could in principle produce the same failure mode.
			if (!FMath::IsFinite(Height))
			{
				UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: non-finite height at (%f, %f) — clamping to 0"), WorldX, WorldY);
				Height = 0.0f;
			}

			if (FMath::Abs(Height) > RepresentableHeightLimit)
			{
				++OutOfRangeCount;
			}
			MinHeight = FMath::Min(MinHeight, Height);
			MaxHeight = FMath::Max(MaxHeight, Height);

			Heightmap.Heights[Row * Resolution + Col] = Height;
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("USWGTerrainSubsystem: BakeHeightmap region origin=(%.1f,%.1f) min=%.1f max=%.1f outOfRange(+/-%.0f)=%d/%d"),
		RegionOrigin.X, RegionOrigin.Y, MinHeight, MaxHeight, RepresentableHeightLimit, OutOfRangeCount, Resolution * Resolution);

	return Heightmap;
}

void USWGTerrainSubsystem::BakeShaderWeights(const FSWGTerrainBakeSource& Source, FSWGBakedHeightmap& Heightmap) const
{
	const int32 Resolution = HeightmapResolution;
	const int32 SampleCount = Resolution * Resolution;
	const TArrayView<const FSWGTerrainLayer> Edits = Source.EditLayers.IsValid() ? MakeArrayView(*Source.EditLayers) : TArrayView<const FSWGTerrainLayer>();

	// One pass, keeping every vertex's full weight map around — re-evaluating
	// the layer tree a second time (rather than storing this) would double
	// the bake cost for no real memory win at this resolution (16384 samples).
	TArray<TMap<int32, float>> PerVertexWeights;
	PerVertexWeights.SetNum(SampleCount);

	TMap<int32, float> TotalWeightByFamily;

	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		const float WorldY = Heightmap.Origin.Y + Row * Heightmap.Spacing;
		for (int32 Col = 0; Col < Resolution; ++Col)
		{
			const float WorldX = Heightmap.Origin.X + Col * Heightmap.Spacing;
			TMap<int32, float>& Weights = PerVertexWeights[Row * Resolution + Col];
			FSWGTerrainEvaluator::GetShaderWeights(*Source.Planet, WorldX, WorldY, Weights, Edits);

			for (const TPair<int32, float>& Pair : Weights)
			{
				TotalWeightByFamily.FindOrAdd(Pair.Key) += Pair.Value;
			}
		}
	}

	TArray<TPair<int32, float>> SortedFamilies;
	for (const TPair<int32, float>& Pair : TotalWeightByFamily)
	{
		if (Pair.Value > 0.0f)
		{
			SortedFamilies.Add(Pair);
		}
	}
	SortedFamilies.Sort([](const TPair<int32, float>& A, const TPair<int32, float>& B) { return A.Value > B.Value; });

	constexpr int32 MaxLayers = 4;
	// One baked tile covers roughly 2km, while roads, plazas, and other hard
	// surface details may occupy only a small part of it. Keep the families
	// that actually paint the tile centre (where this one-tile grid spawns the
	// player) before using the whole-tile totals for the remaining slots.
	const int32 CentreIndex = (Resolution / 2) * Resolution + (Resolution / 2);
	TArray<TPair<int32, float>> CentreFamilies;
	for (const TPair<int32, float>& Pair : PerVertexWeights[CentreIndex])
	{
		if (Pair.Value > 0.0f)
		{
			CentreFamilies.Add(Pair);
		}
	}
	CentreFamilies.Sort([](const TPair<int32, float>& A, const TPair<int32, float>& B) { return A.Value > B.Value; });
	for (const TPair<int32, float>& Pair : CentreFamilies)
	{
		if (Heightmap.ChosenShaderFamilyIds.Num() >= MaxLayers) break;
		Heightmap.ChosenShaderFamilyIds.AddUnique(Pair.Key);
	}
	for (int32 i = 0; i < FMath::Min(SortedFamilies.Num(), MaxLayers); ++i)
	{
		if (Heightmap.ChosenShaderFamilyIds.Num() >= MaxLayers) break;
		Heightmap.ChosenShaderFamilyIds.AddUnique(SortedFamilies[i].Key);
	}

	if (Heightmap.ChosenShaderFamilyIds.Num() == 0)
	{
		// No shader affector ever painted anything at this tile — leave empty,
		// BuildTerrainTileMaterial falls back to the plain default material.
		return;
	}

	Heightmap.ShaderWeightColors.SetNumZeroed(SampleCount);
	for (int32 i = 0; i < SampleCount; ++i)
	{
		const TMap<int32, float>& Weights = PerVertexWeights[i];
		FVector3f Color(0.0f, 0.0f, 0.0f);
		for (int32 Channel = 1; Channel < Heightmap.ChosenShaderFamilyIds.Num(); ++Channel)
		{
			const float* Weight = Weights.Find(Heightmap.ChosenShaderFamilyIds[Channel]);
			Color.Component(Channel - 1) = Weight ? FMath::Clamp(*Weight, 0.0f, 1.0f) : 0.0f;
		}
		Heightmap.ShaderWeightColors[i] = Color;
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: BakeShaderWeights region origin=(%.1f,%.1f) chosen %d family(ies): %s"),
		Heightmap.Origin.X, Heightmap.Origin.Y, Heightmap.ChosenShaderFamilyIds.Num(),
		*FString::JoinBy(Heightmap.ChosenShaderFamilyIds, TEXT(","), [](int32 Id) { return FString::FromInt(Id); }));
}

// ── Water ────────────────────────────────────────────────────────────────

namespace
{
	TAutoConsoleVariable<int32> CVarWater(
		TEXT("swg.Water"), 1,
		TEXT("Spawn the planet's water surfaces (global water table + local water-table boundaries)."));

	TAutoConsoleVariable<float> CVarWaterUVScale(
		TEXT("swg.WaterUVScale"), 8.0f,
		TEXT("Metres per texture repeat on water, multiplied by the boundary's shader size."));

	void CollectWaterBoundaries(const FSWGTerrainLayer& Layer, TArray<const FSWGTerrainBoundary*>& Out)
	{
		if (!Layer.bEnabled) return;
		for (const FSWGTerrainBoundary& Boundary : Layer.Boundaries)
		{
			if (Boundary.bEnabled && Boundary.bLocalWaterTableEnabled
				&& (Boundary.Type == ESWGTerrainBoundaryType::Rectangle || Boundary.Type == ESWGTerrainBoundaryType::Polygon))
			{
				Out.Add(&Boundary);
			}
		}
		for (const FSWGTerrainLayer& Child : Layer.Children)
		{
			CollectWaterBoundaries(Child, Out);
		}
	}
}

void USWGTerrainSubsystem::SpawnWaterBodies()
{
	check(IsInGameThread());
	if (!PlanetData || CVarWater.GetValueOnGameThread() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	WaterActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
	if (!WaterActor)
	{
		return;
	}
#if WITH_EDITOR
	WaterActor->SetActorLabel(TEXT("SWGTerrainWater"));
#endif
	USceneComponent* Root = NewObject<USceneComponent>(WaterActor, TEXT("WaterRoot"));
	WaterActor->SetRootComponent(Root);
	Root->RegisterComponent();

	const FSWGTerrainHeader& Header = PlanetData->Header;
	int32 Surfaces = 0;
	if (Header.bUseGlobalWaterTable)
	{
		const float Half = Header.MapSize * 0.5f;
		AddWaterSurface({ FVector2D(-Half, -Half), FVector2D(Half, -Half), FVector2D(Half, Half), FVector2D(-Half, Half) },
			Header.GlobalWaterTableHeight, Header.GlobalWaterTableShader, Header.GlobalWaterTableShaderSize);
		++Surfaces;
	}

	TArray<const FSWGTerrainBoundary*> Boundaries;
	for (const FSWGTerrainLayer& Layer : PlanetData->TopLevelLayers)
	{
		CollectWaterBoundaries(Layer, Boundaries);
	}
	for (const FSWGTerrainBoundary* Boundary : Boundaries)
	{
		TArray<FVector2D> Outline;
		if (Boundary->Type == ESWGTerrainBoundaryType::Rectangle)
		{
			const float MinX = FMath::Min(Boundary->X0, Boundary->X1), MaxX = FMath::Max(Boundary->X0, Boundary->X1);
			const float MinY = FMath::Min(Boundary->Y0, Boundary->Y1), MaxY = FMath::Max(Boundary->Y0, Boundary->Y1);
			Outline = { FVector2D(MinX, MinY), FVector2D(MaxX, MinY), FVector2D(MaxX, MaxY), FVector2D(MinX, MaxY) };
		}
		else
		{
			Outline = Boundary->Vertices;
		}
		if (Outline.Num() >= 3)
		{
			AddWaterSurface(Outline, Boundary->LocalWaterTableHeight, Boundary->LocalWaterTableShader, Boundary->LocalWaterTableShaderSize);
			++Surfaces;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: %d water surface(s) spawned (global table %s at %.1f, %d local)"),
		Surfaces, Header.bUseGlobalWaterTable ? TEXT("on") : TEXT("off"), Header.GlobalWaterTableHeight, Boundaries.Num());
}

void USWGTerrainSubsystem::AddWaterSurface(const TArray<FVector2D>& RawOutline, float RawHeight, const FString& ShaderName, float ShaderSize)
{
	if (!IsValid(WaterActor) || RawOutline.Num() < 3)
	{
		return;
	}

	TArray<FVector2d> Polygon;
	Polygon.Reserve(RawOutline.Num());
	for (const FVector2D& Point : RawOutline)
	{
		Polygon.Add(FVector2d(Point.X, Point.Y));
	}
	TArray<UE::Geometry::FIndex3i> Triangles;
	PolygonTriangulation::TriangulateSimplePolygon<double>(Polygon, Triangles, false);
	if (Triangles.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: water outline with %d vertices didn't triangulate (shader %s)"), RawOutline.Num(), *ShaderName);
		return;
	}

	// UVs repeat in world space so adjacent bodies and the panner line up.
	const float MetresPerRepeat = FMath::Max(0.25f, CVarWaterUVScale.GetValueOnGameThread() * FMath::Max(ShaderSize, 0.5f));

	UE::Geometry::FDynamicMesh3 Mesh;
	Mesh.EnableAttributes();
	Mesh.Attributes()->SetNumUVLayers(1);
	UE::Geometry::FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->PrimaryUV();
	UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
	TArray<int32> VertexIds, UVIds, NormalIds;
	for (const FVector2D& Point : RawOutline)
	{
		VertexIds.Add(Mesh.AppendVertex(FVector3d(SWGToUnrealSpace(FVector(Point.X, Point.Y, RawHeight)))));
		UVIds.Add(UVs->AppendElement(FVector2f(Point.X / MetresPerRepeat, Point.Y / MetresPerRepeat)));
		NormalIds.Add(Normals->AppendElement(FVector3f::UpVector));
	}
	for (const UE::Geometry::FIndex3i& Triangle : Triangles)
	{
		const int32 TriangleId = Mesh.AppendTriangle(VertexIds[Triangle.A], VertexIds[Triangle.B], VertexIds[Triangle.C]);
		if (TriangleId < 0) continue;
		UVs->SetTriangle(TriangleId, UE::Geometry::FIndex3i(UVIds[Triangle.A], UVIds[Triangle.B], UVIds[Triangle.C]));
		Normals->SetTriangle(TriangleId, UE::Geometry::FIndex3i(NormalIds[Triangle.A], NormalIds[Triangle.B], NormalIds[Triangle.C]));
	}
	// Retail outlines are authored in either winding and the raw->UE axis
	// swap mirrors them again; the surface has to face up either way.
	if (Mesh.TriangleCount() > 0 && Mesh.GetTriNormal(*Mesh.TriangleIndicesItr().begin()).Z < 0.0)
	{
		Mesh.ReverseOrientation(false);
	}

	UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(WaterActor, NAME_None, RF_Transactional);
	Component->SetupAttachment(WaterActor->GetRootComponent());
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	Component->RegisterComponent();
	Component->SetMesh(MoveTemp(Mesh));
	if (UMaterialInterface* Material = GetOrBuildWaterMaterial(ShaderName))
	{
		Component->SetMaterial(0, Material);
	}
}

UMaterialInterface* USWGTerrainSubsystem::GetOrBuildWaterMaterial(const FString& ShaderName)
{
	if (TObjectPtr<UMaterialInterface>* Existing = WaterMaterials.Find(ShaderName))
	{
		return *Existing;
	}

	if (!WaterMaterialParent)
	{
		WaterMaterialParent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGWater.M_SWGWater"));
		if (!WaterMaterialParent)
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: M_SWGWater not found — water surfaces get the default material"));
			return nullptr;
		}
	}

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(WaterMaterialParent, this);
	if (!ShaderName.IsEmpty())
	{
		if (UTexture2D* Diffuse = GetOrLoadShaderTexture(ShaderName, false))
		{
			MID->SetTextureParameterValue(TEXT("Diffuse"), Diffuse);
		}
		if (UTexture2D* Normal = GetOrLoadShaderTexture(ShaderName, true))
		{
			MID->SetTextureParameterValue(TEXT("Normal"), Normal);
		}
	}
	WaterMaterials.Add(ShaderName, MID);
	return MID;
}

UTexture2D* USWGTerrainSubsystem::GetOrLoadShaderTexture(const FString& LayerName, bool bNormalMap)
{
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *LayerName, bNormalMap ? TEXT("normal") : TEXT("diffuse"));
	if (TObjectPtr<UTexture2D>* Existing = LoadedShaderTextures.Find(CacheKey))
	{
		return *Existing;
	}

	// Cache the miss too (as nullptr) so a bad/missing texture doesn't retry
	// (and re-log) on every tile that happens to reference the same family.
	UTexture2D* Result = nullptr;

	FString VirtualPath;
	const FString ShaderPath = FString::Printf(TEXT("shader/%s.sht"), *LayerName);
	if (TreSubsystem && TreSubsystem->FileExists(ShaderPath))
	{
		FSWGShaderData ShaderData;
		if (FSWGShaderReader::ReadShader(TreSubsystem->CreateIffReader(ShaderPath), ShaderData))
		{
			const ESWGShaderTextureUsage Usage = bNormalMap
				? ESWGShaderTextureUsage::Normal
				: ESWGShaderTextureUsage::Diffuse;
			if (const FSWGShaderTexture* Texture = ShaderData.FindTexture(Usage))
			{
				VirtualPath = Texture->VirtualPath;
			}
		}
	}

	// Older terrain families without a .sht use the direct texture convention.
	if (VirtualPath.IsEmpty())
	{
		VirtualPath = FString::Printf(TEXT("texture/%s%s.dds"), *LayerName, bNormalMap ? TEXT("_n") : TEXT(""));
	}
	if (TreSubsystem && TreSubsystem->FileExists(VirtualPath))
	{
		const TArray<uint8> Bytes = TreSubsystem->ExtractFile(VirtualPath);
		Result = FSWGDDSTextureLoader::LoadTexture2D(Bytes, FName(*VirtualPath), /*bSRGB=*/!bNormalMap);
		if (Result)
		{
			// Terrain UVs use repeating world-space coordinates. Runtime DDS
			// textures are configured to wrap by FSWGDDSTextureLoader before its
			// initial resource creation.
			Result->AddressX = TA_Wrap;
			Result->AddressY = TA_Wrap;
		}
	}

	if (!Result)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: failed to load shader texture '%s'"), *VirtualPath);
	}

	LoadedShaderTextures.Add(CacheKey, Result);
	return Result;
}

UMaterialInterface* USWGTerrainSubsystem::BuildTerrainTileMaterial(const FSWGBakedHeightmap& Heightmap)
{
	if (Heightmap.ChosenShaderFamilyIds.Num() == 0)
	{
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}

	if (!TerrainBlendMaterial)
	{
		TerrainBlendMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/SWGEmu/Materials/M_SWGTerrainBlend.M_SWGTerrainBlend"));
	}

	if (!TerrainBlendMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: M_SWGTerrainBlend not found — falling back to plain default material"));
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(TerrainBlendMaterial, this);

	static const FName LayerParamNames[4] = { TEXT("Layer0"), TEXT("Layer1"), TEXT("Layer2"), TEXT("Layer3") };
	static const FName NormalParamNames[4] = { TEXT("Layer0Normal"), TEXT("Layer1Normal"), TEXT("Layer2Normal"), TEXT("Layer3Normal") };

	for (int32 Channel = 0; Channel < Heightmap.ChosenShaderFamilyIds.Num(); ++Channel)
	{
		const FSWGShaderFamily* Family = PlanetData->FindShaderFamily(Heightmap.ChosenShaderFamilyIds[Channel]);
		if (!Family || Family->LayerNames.Num() == 0)
		{
			continue;
		}
		UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: terrain material slot %d uses family %d '%s', layer '%s'"),
			Channel, Heightmap.ChosenShaderFamilyIds[Channel], *Family->Name, *Family->LayerNames[0]);

		if (UTexture2D* Texture = GetOrLoadShaderTexture(Family->LayerNames[0]))
		{
			MID->SetTextureParameterValue(LayerParamNames[Channel], Texture);
		}
		if (UTexture2D* NormalTexture = GetOrLoadShaderTexture(Family->LayerNames[0], /*bNormalMap=*/true))
		{
			MID->SetTextureParameterValue(NormalParamNames[Channel], NormalTexture);
		}
	}

	return MID;
}

ALandscape* USWGTerrainSubsystem::SpawnLandscapeActor(const FVector& GridOrigin, float Spacing)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: no valid world to spawn landscape in"));
		return nullptr;
	}

	// A single subsection per component — SubsectionSizeQuads+1 must be a power
	// of two (LandscapeComponent.h:449); HeightmapResolution=128 samples gives
	// exactly that (127 quads). ComponentSizeQuads/SubsectionSizeQuads/NumSubsections
	// are shared by every component the grid adds to this actor.
	const int32 ComponentVerts = HeightmapResolution;
	const int32 SubsectionSizeQuads = ComponentVerts - 1;
	const int32 NumSubsections = 1;
	const int32 ComponentSizeQuads = NumSubsections * SubsectionSizeQuads;

	// XY: each quad spans Spacing world units. Z: HeightZScale (see its own
	// comment) — packed heights (via LandscapeDataAccess::GetTexHeight, the
	// exact inverse of GetLocalHeight) reconstruct in world-height units once
	// scaled by this, since GetTexHeight's own range is a fixed +/-256 *local*
	// units regardless of our chosen Z scale.
	const FVector DesiredScale(Spacing, Spacing, HeightZScale);

	// ALandscapeProxy's constructor unconditionally sets its own RootComponent
	// scale (128,128,256), which doesn't compose predictably with a pre-divided
	// spawn-transform scale. Spawn at identity scale instead, then force the
	// exact scale afterward; Static mobility blocks SetActorScale3D, so flip
	// mobility around the call (a one-time setup step, not a runtime move).
	const FTransform SpawnTransform(FQuat::Identity, GridOrigin, FVector::OneVector);
	ALandscape* Landscape = Cast<ALandscape>(World->SpawnActor(ALandscape::StaticClass(), &SpawnTransform));
	if (!Landscape)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to spawn ALandscape"));
		return nullptr;
	}

	USceneComponent* LandscapeRoot = Landscape->GetRootComponent();
	const EComponentMobility::Type OriginalMobility = LandscapeRoot->Mobility;
	LandscapeRoot->SetMobility(EComponentMobility::Movable);
	Landscape->SetActorRelativeScale3D(DesiredScale);
	LandscapeRoot->SetMobility(OriginalMobility);

	Landscape->ComponentSizeQuads = ComponentSizeQuads;
	Landscape->SubsectionSizeQuads = SubsectionSizeQuads;
	Landscape->NumSubsections = NumSubsections;

	// Placeholder so the landscape isn't simply invisible — a real terrain
	// material (and the shader/flora work it implies) is explicitly deferred,
	// see world-object-plan.html "Shader/flora — no server-side ground truth".
	Landscape->LandscapeMaterial = UMaterial::GetDefaultMaterial(MD_Surface);

	return Landscape;
}

void USWGTerrainSubsystem::AddLandscapeComponent(ALandscape* Landscape, const FSWGBakedHeightmap& Heightmap, const FIntPoint& SectionBase)
{
	if (Heightmap.Heights.Num() != HeightmapResolution * HeightmapResolution)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: heightmap has %d samples, expected %d — refusing to add component"),
			Heightmap.Heights.Num(), HeightmapResolution * HeightmapResolution);
		return;
	}

	const int32 ComponentVerts = HeightmapResolution;
	const int32 SubsectionSizeQuads = ComponentVerts - 1;
	const int32 NumSubsections = 1;
	const int32 ComponentSizeQuads = NumSubsections * SubsectionSizeQuads;

	ULandscapeComponent* Component = NewObject<ULandscapeComponent>(Landscape, NAME_None, RF_Transactional);
	Component->Init(SectionBase.X, SectionBase.Y, ComponentSizeQuads, NumSubsections, SubsectionSizeQuads);

	// Confirmed via ULandscapeComponent::GetNumRelevantMips (Landscape.cpp:2119-2127):
	// NumRelevantMips = NumTextureMips - (NumSubsections > 1 ? 2 : 1), and it must be
	// > 0 or the component asserts/crashes — a single mip (what we shipped originally)
	// fails this outright. Build the full chain down to 1x1.
	const int32 NumTextureMips = FMath::CeilLogTwo(ComponentVerts) + 1; // 128 -> 8 (128,64,...,2,1)
	const int32 NumRelevantMips = (NumSubsections > 1) ? (NumTextureMips - 2) : (NumTextureMips - 1);

	TArray<TArray<float>> MipHeights;
	BuildMipHeightPyramid(Heightmap.Heights, ComponentVerts, NumTextureMips, MipHeights);

	// Real normals derived from the heightmap are a follow-up — a flat default
	// (matching GetDefaultPackedHeightColor's convention) is enough to get this rendering.
	const FColor DefaultNormal = LandscapeDataAccess::GetDefaultPackedHeightColor();

	// Mip 0 goes through CreateTransient; additional mips are appended the same
	// way CreateTransient builds its own (new FTexture2DMipMap + BulkData
	// lock/realloc/unlock). Name must be unique per component, or every
	// component in the grid resolves to (and overwrites) the same transient object.
	const TArray<FColor> Mip0Pixels = PackHeightMip(MipHeights[0], DefaultNormal);
	const FName TextureName(*FString::Printf(TEXT("SWGTerrainHeightmap_%d_%d"), SectionBase.X, SectionBase.Y));

	// Deliberately called with NO image data — see FSWGDDSTextureLoader::
	// LoadTexture2D's matching comment: CreateTransient() calls its own
	// internal UpdateResource() whenever image data is passed, which races
	// against the UpdateResource() below (called once the full mip chain is
	// appended) since the two enqueue separate RHI resource-inits for the
	// same UTexture2D. Passing no data still yields an allocated mip 0 we
	// populate ourselves, same as every mip after it.
	UTexture2D* HeightmapTexture = UTexture2D::CreateTransient(
		ComponentVerts, ComponentVerts, PF_B8G8R8A8, TextureName);

	if (!HeightmapTexture)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to create heightmap texture"));
		return;
	}

	{
		const int64 Mip0Bytes = (int64)Mip0Pixels.Num() * sizeof(FColor);
		FTexture2DMipMap& Mip0Map = HeightmapTexture->GetPlatformData()->Mips[0];
		Mip0Map.BulkData.Lock(LOCK_READ_WRITE);
		void* DestData = Mip0Map.BulkData.Realloc(Mip0Bytes);
		FMemory::Memcpy(DestData, Mip0Pixels.GetData(), Mip0Bytes);
		Mip0Map.BulkData.Unlock();
	}

#if WITH_EDITOR
	// Editor-only: ULandscapeTextureHash::GetHash asserts if Source is
	// unpopulated when no hash asset user data is attached (ours never has any),
	// and CreateTransient only builds PlatformData, never Source. Only mip 0
	// needs populating since CalculateTextureHash64 only reads mip 0.
	HeightmapTexture->Source.Init(ComponentVerts, ComponentVerts, /*NewNumSlices=*/1, /*NewNumMips=*/1, TSF_BGRA8,
		reinterpret_cast<const uint8*>(Mip0Pixels.GetData()));
#endif

	for (int32 Mip = 1; Mip < NumTextureMips; ++Mip)
	{
		const int32 MipDim = ComponentVerts >> Mip;
		const TArray<FColor> MipPixels = PackHeightMip(MipHeights[Mip], DefaultNormal);
		const int64 MipBytes = (int64)MipPixels.Num() * sizeof(FColor);

		FTexture2DMipMap* MipMap = new FTexture2DMipMap(MipDim, MipDim, 1);
		HeightmapTexture->GetPlatformData()->Mips.Add(MipMap);
		MipMap->BulkData.Lock(LOCK_READ_WRITE);
		void* DestData = MipMap->BulkData.Realloc(MipBytes);
		FMemory::Memcpy(DestData, MipPixels.GetData(), MipBytes);
		MipMap->BulkData.Unlock();
	}

	HeightmapTexture->UpdateResource();

	Component->SetHeightmap(HeightmapTexture);

	// Single dedicated texture per component (no shared atlas) — confirmed formula
	// from LandscapeEdit.cpp's own (editor-only) single-component-texture case.
	Component->HeightmapScaleBias = FVector4(1.0f / (float)ComponentVerts, 1.0f / (float)ComponentVerts, 0.0f, 0.0f);

	// Reimplements the editor-only MipToMipMaxDeltas computation (see helper
	// comments above) — required for Landscape's LOD morphing; leaving this empty
	// is what caused the "-93 into an array of size 1" crash.
	TArray<double> MipToMipMaxDeltas;
	ComputeMipToMipMaxDeltas(MipHeights, ComponentVerts, NumRelevantMips, MipToMipMaxDeltas);
	Component->MipToMipMaxDeltas = MoveTemp(MipToMipMaxDeltas);

	// One entry per relevant LOD (FLandscapeComponentSceneProxy asserts
	// LODIndexToMaterialIndex.Num() == MaxLOD + 1, LandscapeRender.cpp:1480, where
	// MaxLOD = CeilLogTwo(SubsectionSizeQuads + 1) - 1 == NumRelevantMips - 1 for
	// our single-subsection case). We only ever assign one material, so every LOD
	// points at slot 0.
	Component->LODIndexToMaterialIndex.Init(0, NumRelevantMips);

	Component->RegisterComponent();

	// Collision is explicitly deferred (see world-object-plan.html "Collision-data
	// research pass") — this landscape renders but is not yet walkable.
	if (UE_LOG_ACTIVE(LogTemp, Log))
	{
		FString OriginStr = Heightmap.Origin.ToString();
		UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: added component at %s (SectionBase %d,%d) with %d mips (no collision yet)"),
			*OriginStr, SectionBase.X, SectionBase.Y, NumTextureMips);
	}
}

bool FSWGTerrainHole::Contains(const FVector2D& Point) const
{
	// Un-rotate into the hole's frame — FSWGTerrainModifier's LocalToWorld, backwards.
	const FVector2D Delta = Point - Center;
	const float SinYaw = FMath::Sin(YawRadians);
	const float CosYaw = FMath::Cos(YawRadians);
	const FVector2D Local(Delta.X * CosYaw + Delta.Y * SinYaw, -Delta.X * SinYaw + Delta.Y * CosYaw);

	return FMath::Abs(Local.X) <= Extents.X && FMath::Abs(Local.Y) <= Extents.Y;
}

FBox2D FSWGTerrainHole::GetWorldBounds() const
{
	const float SinYaw = FMath::Abs(FMath::Sin(YawRadians));
	const float CosYaw = FMath::Abs(FMath::Cos(YawRadians));
	const FVector2D Half(
		Extents.X * CosYaw + Extents.Y * SinYaw,
		Extents.X * SinYaw + Extents.Y * CosYaw);

	return FBox2D(Center - Half, Center + Half);
}

namespace
{
	/** One grid sample's element ids across the mesh and its overlays. */
	struct FSWGTerrainVertex
	{
		int32 Vertex = INDEX_NONE;
		int32 Normal = INDEX_NONE;
		int32 UV = INDEX_NONE;
		int32 Color = INDEX_NONE;
	};

	/**
	 * Builds one tile's grid into Mesh, leaving out whatever Holes cover. A quad
	 * a hole edge crosses is split Subdivisions ways per axis and its sub-quads
	 * kept or dropped individually; sub-samples interpolate that quad's own
	 * corners, so it still meets its unsplit neighbours exactly. Any thread.
	 */
	void BuildTerrainTileGeometry(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FSWGBakedHeightmap& Heightmap,
		int32 Resolution,
		const FVector& LocalOrigin,
		const FVector2f& UVOrigin,
		const TArray<FSWGTerrainHole>& Holes,
		int32 Subdivisions)
	{
		using namespace UE::Geometry;

		// EditMesh hands back whatever the component already holds, and
		// everything below appends. On the initial spawn that mesh is empty, but
		// on a regeneration it still holds the previous bake — without this the
		// tile ends up with two overlapping terrain sheets, the stale
		// un-flattened one z-fighting the new one. Clear() also drops the
		// attribute set, so EnableAttributes has to follow it, not precede it.
		Mesh.Clear();
		Mesh.EnableAttributes();
		FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->PrimaryUV();

		// Vertex colors carry this tile's shader-family blend weights (R/G/B
		// = family[1]/[2]/[3]'s paint weight; family[0]'s weight is implicit,
		// 1-R-G-B, computed in the material) — see BakeShaderWeights.
		const bool bHasShaderWeights = Heightmap.ShaderWeightColors.Num() == Resolution * Resolution;
		if (bHasShaderWeights)
		{
			// UDynamicMeshComponent's VertexColor material node consumes the
			// primary color overlay, not FDynamicMesh3's legacy color buffer.
			Mesh.Attributes()->EnablePrimaryColors();
		}
		FDynamicMeshColorOverlay* Colors = bHasShaderWeights ? Mesh.Attributes()->PrimaryColors() : nullptr;

		// Grid coordinates rather than indices: the hole-cutting path appends
		// samples at fractional (Col, Row).
		auto AppendSample = [&](double Col, double Row, float Height, const FVector3f& Weight)
		{
			FSWGTerrainVertex Ids;

			// Everything feeding this (LocalOrigin, Spacing, baked Heights) is
			// raw space, matching the .trn's own units — SWGToUnrealSpace
			// rotates and scales into final UE units right here, at the actual
			// vertex-placement boundary.
			const FVector3d Pos = SWGToUnrealSpace(FVector(
				LocalOrigin.X + Col * Heightmap.Spacing,
				LocalOrigin.Y + Row * Heightmap.Spacing,
				Height));

			Ids.Vertex = Mesh.AppendVertex(Pos);
			Ids.Normal = Normals->AppendElement(FVector3f(0, 0, 1));
			Ids.UV = UVs->AppendElement(FVector2f(
				UVOrigin.X + (float)(Col * Heightmap.Spacing / TerrainTextureRepeatWorldSize),
				UVOrigin.Y + (float)(Row * Heightmap.Spacing / TerrainTextureRepeatWorldSize)));

			if (Colors)
			{
				Ids.Color = Colors->AppendElement(FVector4f(Weight.X, Weight.Y, Weight.Z, 1.0f));
			}

			return Ids;
		};

		auto AppendTriangle = [&](const FSWGTerrainVertex& A, const FSWGTerrainVertex& B, const FSWGTerrainVertex& C)
		{
			const int32 Tri = Mesh.AppendTriangle(A.Vertex, B.Vertex, C.Vertex);
			if (Tri < 0)
			{
				return;
			}

			Normals->SetTriangle(Tri, FIndex3i(A.Normal, B.Normal, C.Normal));
			UVs->SetTriangle(Tri, FIndex3i(A.UV, B.UV, C.UV));
			if (Colors)
			{
				Colors->SetTriangle(Tri, FIndex3i(A.Color, B.Color, C.Color));
			}
		};

		TArray<FSWGTerrainVertex> GridSamples;
		GridSamples.SetNumUninitialized(Resolution * Resolution);

		for (int32 Row = 0; Row < Resolution; ++Row)
		{
			for (int32 Col = 0; Col < Resolution; ++Col)
			{
				const int32 Idx = Row * Resolution + Col;
				GridSamples[Idx] = AppendSample(Col, Row, Heightmap.Heights[Idx],
					bHasShaderWeights ? Heightmap.ShaderWeightColors[Idx] : FVector3f::ZeroVector);
			}
		}

		const FVector2D TileOrigin(Heightmap.Origin.X, Heightmap.Origin.Y);
		auto GridToWorld = [&TileOrigin, &Heightmap](double Col, double Row)
		{
			return TileOrigin + FVector2D(Col, Row) * Heightmap.Spacing;
		};

		auto IsInAnyHole = [&Holes](const FVector2D& World)
		{
			return Holes.ContainsByPredicate([&World](const FSWGTerrainHole& Hole) { return Hole.Contains(World); });
		};

		for (int32 Row = 0; Row < Resolution - 1; ++Row)
		{
			for (int32 Col = 0; Col < Resolution - 1; ++Col)
			{
				const int32 I00 = Row * Resolution + Col;
				const int32 I10 = Row * Resolution + (Col + 1);
				const int32 I01 = (Row + 1) * Resolution + Col;
				const int32 I11 = (Row + 1) * Resolution + (Col + 1);

				const FBox2D QuadBounds(GridToWorld(Col, Row), GridToWorld(Col + 1, Row + 1));
				const bool bTouchesHole = Holes.ContainsByPredicate(
					[&QuadBounds](const FSWGTerrainHole& Hole) { return Hole.GetWorldBounds().Intersect(QuadBounds); });

				// Col runs along UE +Y and Row along UE +X (raw x east / y
				// north -> UE Y / X), so this order keeps the faces pointing up.
				if (!bTouchesHole)
				{
					AppendTriangle(GridSamples[I00], GridSamples[I10], GridSamples[I01]);
					AppendTriangle(GridSamples[I10], GridSamples[I11], GridSamples[I01]);
					continue;
				}

				const int32 N = FMath::Max(Subdivisions, 1);
				TArray<FSWGTerrainVertex> Sub;
				Sub.SetNumUninitialized((N + 1) * (N + 1));

				for (int32 SubRow = 0; SubRow <= N; ++SubRow)
				{
					for (int32 SubCol = 0; SubCol <= N; ++SubCol)
					{
						const double U = (double)SubCol / N;
						const double V = (double)SubRow / N;

						const float Height = BilinearSample(
							Heightmap.Heights[I00], Heightmap.Heights[I10],
							Heightmap.Heights[I01], Heightmap.Heights[I11], U, V);

						const FVector3f Weight = bHasShaderWeights
							? BilinearSample(
								Heightmap.ShaderWeightColors[I00], Heightmap.ShaderWeightColors[I10],
								Heightmap.ShaderWeightColors[I01], Heightmap.ShaderWeightColors[I11], U, V)
							: FVector3f::ZeroVector;

						Sub[SubRow * (N + 1) + SubCol] = AppendSample(Col + U, Row + V, Height, Weight);
					}
				}

				for (int32 SubRow = 0; SubRow < N; ++SubRow)
				{
					for (int32 SubCol = 0; SubCol < N; ++SubCol)
					{
						// Centre decides the sub-quad; clipping it against the
						// hole outline would buy accuracy nothing else uses.
						if (IsInAnyHole(GridToWorld(Col + (SubCol + 0.5) / N, Row + (SubRow + 0.5) / N)))
						{
							continue;
						}

						const int32 S00 = SubRow * (N + 1) + SubCol;
						const int32 S10 = SubRow * (N + 1) + (SubCol + 1);
						const int32 S01 = (SubRow + 1) * (N + 1) + SubCol;
						const int32 S11 = (SubRow + 1) * (N + 1) + (SubCol + 1);

						AppendTriangle(Sub[S00], Sub[S10], Sub[S01]);
						AppendTriangle(Sub[S10], Sub[S11], Sub[S01]);
					}
				}
			}
		}

		// Samples whose triangles were all dropped. Isolated vertices are legal
		// here but reach collision cooking as degenerate input.
		TArray<int32> Isolated;
		for (const int32 VertexId : Mesh.VertexIndicesItr())
		{
			if (Mesh.GetVtxTriangleCount(VertexId) == 0)
			{
				Isolated.Add(VertexId);
			}
		}
		for (const int32 VertexId : Isolated)
		{
			Mesh.RemoveVertex(VertexId);
		}
	}
}

FIntPoint USWGTerrainSubsystem::TileCoordAt(const FVector2D& RawPosition)
{
	return FIntPoint(FMath::FloorToInt(RawPosition.X / HeightmapWorldExtent), FMath::FloorToInt(RawPosition.Y / HeightmapWorldExtent));
}

FVector USWGTerrainSubsystem::TileOrigin(const FIntPoint& Coord)
{
	return FVector(Coord.X * HeightmapWorldExtent, Coord.Y * HeightmapWorldExtent, 0.0f);
}

FBox2D USWGTerrainSubsystem::TileBounds(const FIntPoint& Coord)
{
	const FVector2D Min(Coord.X * HeightmapWorldExtent, Coord.Y * HeightmapWorldExtent);
	return FBox2D(Min, Min + FVector2D(HeightmapWorldExtent, HeightmapWorldExtent));
}

bool USWGTerrainSubsystem::IsTileOnMap(const FIntPoint& Coord) const
{
	const float HalfMap = PlanetData.IsValid() ? PlanetData->Header.MapSize * 0.5f : 8192.0f;
	const FBox2D Bounds = TileBounds(Coord);
	return Bounds.Max.X > -HalfMap && Bounds.Min.X < HalfMap && Bounds.Max.Y > -HalfMap && Bounds.Min.Y < HalfMap;
}

bool USWGTerrainSubsystem::GetStreamingCenter(FVector2D& OutRawPosition) const
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (Pawn)
	{
		const FVector Raw = SWGToRawSpace(Pawn->GetActorLocation());
		OutRawPosition = FVector2D(Raw.X, Raw.Y);
		return true;
	}

	OutRawPosition = SpawnRawPosition;
	return true;
}

void USWGTerrainSubsystem::UpdateStreaming()
{
	check(IsInGameThread());

	if (!bTerrainDataCached || !IsValid(TerrainMeshActor))
	{
		return;
	}

	FVector2D Center;
	GetStreamingCenter(Center);

	// Lead the player by half a tile along the heading: the ring is the same
	// size either way, it just crosses into the next tile column early on the
	// side being walked towards (and drops the trailing one early). A second
	// ring around an "ahead" point instead unioned onto this one and, with the
	// unload radius one tile out, never shed those tiles again — a wobbling
	// heading grew the loaded set to the full radius+1 square over time.
	FVector2D EffectiveCenter = Center;
	if (bHasLastStreamingCenter)
	{
		const FVector2D Heading = Center - LastStreamingCenter;
		if (Heading.SizeSquared() > 1.0f)
		{
			EffectiveCenter = Center + Heading.GetSafeNormal() * (HeightmapWorldExtent * 0.5f);
		}
	}
	LastStreamingCenter = Center;
	bHasLastStreamingCenter = true;

	const FIntPoint CenterTile = TileCoordAt(EffectiveCenter);

	const int32 LoadRadius = FMath::Max(0, CVarTerrainLoadRadius.GetValueOnGameThread());
	const int32 UnloadRadius = FMath::Max(LoadRadius, CVarTerrainUnloadRadius.GetValueOnGameThread());

	TSet<FIntPoint> Wanted;
	for (int32 OffsetY = -LoadRadius; OffsetY <= LoadRadius; ++OffsetY)
	{
		for (int32 OffsetX = -LoadRadius; OffsetX <= LoadRadius; ++OffsetX)
		{
			const FIntPoint Coord = CenterTile + FIntPoint(OffsetX, OffsetY);
			if (IsTileOnMap(Coord))
			{
				Wanted.Add(Coord);
			}
		}
	}
	// The spawn 3x3 is only pinned until it has reported ready.
	if (!bInitialTilesReported)
	{
		Wanted.Append(InitialTiles);
	}

	for (const FIntPoint& Coord : Wanted)
	{
		if (!Tiles.Contains(Coord))
		{
			Tiles.Add(Coord).WantedEditVersion = EditVersion;
			BakeQueue.Add(Coord);
		}
	}

	TArray<FIntPoint> ToUnload;
	for (const TPair<FIntPoint, FSWGTerrainTile>& Pair : Tiles)
	{
		const FIntPoint Delta = Pair.Key - CenterTile;
		const int32 Distance = FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y));
		if (Distance > UnloadRadius && !Wanted.Contains(Pair.Key))
		{
			ToUnload.Add(Pair.Key);
		}
	}
	for (const FIntPoint& Coord : ToUnload)
	{
		UnloadTile(Coord);
	}

	// Static objects get a tighter ring than the ground: the server only
	// sends network objects within ~192 m (ZoneServer::CLOSEOBJECTRANGE), so
	// a distant city is scenery, and its hundreds of actors and meshes are
	// the expensive part of a tile. Terrain keeps the horizon.
	const int32 SnapshotLoadRadius = FMath::Clamp(CVarSnapshotLoadRadius.GetValueOnGameThread(), 0, LoadRadius);
	const int32 SnapshotUnloadRadius = FMath::Max(SnapshotLoadRadius, CVarSnapshotUnloadRadius.GetValueOnGameThread());

	for (TPair<FIntPoint, FSWGTerrainTile>& Pair : Tiles)
	{
		const FIntPoint Delta = Pair.Key - CenterTile;
		const int32 Distance = FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y));
		FSWGTerrainTile& Tile = Pair.Value;

		if (Distance <= SnapshotLoadRadius && !Tile.bSnapshotSpawned && !Tile.bSnapshotResolveInFlight)
		{
			StartSnapshotResolve(Pair.Key);
		}
		else if (Distance > SnapshotUnloadRadius && Tile.bSnapshotSpawned)
		{
			const int32 ActorCount = Tile.SnapshotActors.Num();
			DestroySnapshotActors(Tile);
			if (ActorCount > 0)
			{
				UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: tile (%d,%d) %d static actor(s) unloaded (terrain kept)"), Pair.Key.X, Pair.Key.Y, ActorCount);
			}
		}
	}

	PumpBakeQueue();
}

void USWGTerrainSubsystem::StartSnapshotResolve(const FIntPoint& Coord)
{
	check(IsInGameThread());

	FSWGTerrainTile* Tile = Tiles.Find(Coord);
	if (!Tile || !SnapshotNodesByTile.Contains(Coord))
	{
		// Nothing authored here — mark it done so the sweep stops asking.
		if (Tile)
		{
			Tile->bSnapshotSpawned = true;
		}
		return;
	}

	Tile->bSnapshotResolveInFlight = true;
	const int32 Generation = TerrainGeneration;

	// Template IFF reads and the form-tag table lookup are all the resolve
	// does; both are already exercised off the game thread by the mesh path.
	Async(EAsyncExecution::ThreadPool, [this, Coord, Generation]()
		{
			TArray<FSWGWorldSnapshotSpawnInfo> Objects = ResolveSnapshotObjectsForTile(Coord);

			AsyncTask(ENamedThreads::GameThread, [this, Coord, Generation, Objects = MoveTemp(Objects)]() mutable
				{
					if (Generation != TerrainGeneration)
					{
						return;
					}
					FSWGTerrainTile* LandedTile = Tiles.Find(Coord);
					if (!LandedTile)
					{
						return;
					}
					LandedTile->bSnapshotResolveInFlight = false;

					// Walked away while this resolved — let the next sweep ask again if needed.
					FVector2D Center;
					GetStreamingCenter(Center);
					const FIntPoint Delta = Coord - TileCoordAt(Center);
					if (FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y)) > CVarSnapshotUnloadRadius.GetValueOnGameThread())
					{
						return;
					}

					if (!LandedTile->bSnapshotSpawned)
					{
						QueueSnapshotObjectsForTile(Coord, *LandedTile, MoveTemp(Objects));
					}
				});
		});
}

void USWGTerrainSubsystem::PumpBakeQueue()
{
	check(IsInGameThread());

	if (BakeQueue.IsEmpty() || BakesInFlight >= MaxBakesInFlight)
	{
		return;
	}

	FVector2D Center;
	GetStreamingCenter(Center);
	const FIntPoint CenterTile = TileCoordAt(Center);

	TArray<FIntPoint> Ordered = BakeQueue.Array();
	Ordered.Sort([&CenterTile](const FIntPoint& Left, const FIntPoint& Right)
		{
			const FIntPoint LeftDelta = Left - CenterTile;
			const FIntPoint RightDelta = Right - CenterTile;
			return LeftDelta.X * LeftDelta.X + LeftDelta.Y * LeftDelta.Y < RightDelta.X * RightDelta.X + RightDelta.Y * RightDelta.Y;
		});

	for (const FIntPoint& Coord : Ordered)
	{
		if (BakesInFlight >= MaxBakesInFlight)
		{
			break;
		}

		FSWGTerrainTile* Tile = Tiles.Find(Coord);
		BakeQueue.Remove(Coord);
		if (!Tile || Tile->bBakeInFlight)
		{
			continue;
		}

		StartTileBake(Coord);
	}
}

void USWGTerrainSubsystem::StartTileBake(const FIntPoint& Coord)
{
	check(IsInGameThread());

	FSWGTerrainTile* Tile = Tiles.Find(Coord);
	if (!Tile)
	{
		return;
	}

	Tile->bBakeInFlight = true;
	++BakesInFlight;

	const int32 Generation = TerrainGeneration;
	FSWGTerrainBakeSource Source = MakeBakeSource();
	const int32 BakedVersion = Source.EditVersion;

	Async(EAsyncExecution::ThreadPool, [this, Coord, Generation, BakedVersion, Source = MoveTemp(Source)]()
		{
			FSWGTerrainTileBuild Build = BakeTerrainTile(Source, Coord);

			AsyncTask(ENamedThreads::GameThread, [this, Coord, Generation, BakedVersion, Build = MoveTemp(Build)]() mutable
				{
					OnTileBakeFinished(Coord, Generation, BakedVersion, Build);
				});
		});
}

void USWGTerrainSubsystem::OnTileBakeFinished(const FIntPoint& Coord, int32 Generation, int32 BakedVersion, FSWGTerrainTileBuild& Build)
{
	check(IsInGameThread());

	--BakesInFlight;

	if (Generation != TerrainGeneration)
	{
		// Zone changed while this baked — nothing to apply it to.
		PumpBakeQueue();
		return;
	}

	FSWGTerrainTile* Tile = Tiles.Find(Coord);
	if (!Tile)
	{
		// Unloaded while baking.
		PumpBakeQueue();
		return;
	}

	Tile->bBakeInFlight = false;

	// Show it even if stale: a slightly-old surface beats a hole while the
	// re-bake runs. Never regress though — a stale result must not replace a
	// newer one that landed first.
	if (Tile->LiveEditVersion <= BakedVersion)
	{
		ApplyTerrainTileBuild(Coord, *Tile, Build);
		Tile->LiveEditVersion = BakedVersion;
	}

	if (Tile->WantedEditVersion > BakedVersion)
	{
		BakeQueue.Add(Coord);
	}

	if (!bInitialTilesReported && !InitialTiles.IsEmpty())
	{
		bool bAllLive = true;
		for (const FIntPoint& Initial : InitialTiles)
		{
			const FSWGTerrainTile* InitialTile = Tiles.Find(Initial);
			if (!InitialTile || InitialTile->LiveEditVersion < 0)
			{
				bAllLive = false;
				break;
			}
		}
		if (bAllLive)
		{
			bInitialTilesReported = true;
			UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: %d spawn tile(s) live — terrain ready"), InitialTiles.Num());
			OnTerrainReady.Broadcast();
		}
	}

	PumpBakeQueue();
}

void USWGTerrainSubsystem::UnloadTile(const FIntPoint& Coord)
{
	check(IsInGameThread());

	FSWGTerrainTile* Tile = Tiles.Find(Coord);
	if (!Tile)
	{
		return;
	}

	DestroySnapshotActors(*Tile);

	if (UDynamicMeshComponent* Component = Tile->Component)
	{
		// Kept registered and attached, just emptied: the next tile to load
		// takes it over in AcquireTileComponent rather than paying for a new one.
		Component->SetMesh(UE::Geometry::FDynamicMesh3());
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetVisibility(false);
		PooledTileComponents.Add(Component);
	}

	BakeQueue.Remove(Coord);
	// An in-flight bake finds no tile when it lands and is dropped.
	Tiles.Remove(Coord);

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: unloaded tile (%d,%d) — %d live, %d pooled"), Coord.X, Coord.Y, Tiles.Num(), PooledTileComponents.Num());
}

UDynamicMeshComponent* USWGTerrainSubsystem::AcquireTileComponent(const FIntPoint& Coord)
{
	check(IsInGameThread());

	if (!IsValid(TerrainMeshActor))
	{
		return nullptr;
	}

	UDynamicMeshComponent* MeshComponent = nullptr;
	if (!PooledTileComponents.IsEmpty())
	{
		MeshComponent = PooledTileComponents.Pop();
		MeshComponent->SetVisibility(true);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else
	{
		MeshComponent = NewObject<UDynamicMeshComponent>(TerrainMeshActor, NAME_None, RF_Transactional);
		MeshComponent->SetupAttachment(TerrainMeshActor->GetRootComponent());

		// Cooking collision synchronously is most of what made a tile cost
		// hundreds of ms of game thread, and nothing needs it the instant it appears.
		MeshComponent->bUseAsyncCooking = true;

		// Deliberately NOT calling SetColorOverrideMode(VertexColors) — any
		// non-None mode makes the scene proxy force-substitute the engine's
		// vertex-color debug material regardless of what's assigned. Vertex
		// color still uploads to the GPU with ColorMode at its default None,
		// which is all M_SWGTerrainBlend's VertexColor node needs.

		// The terrain's own baked triangle mesh is its collision shape
		// (complex-as-simple) — it's already a heightfield, there's no cheaper
		// "simple" approximation worth building separately.
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetCollisionObjectType(ECC_WorldStatic);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
		MeshComponent->RegisterComponent();
		MeshComponent->EnableComplexAsSimpleCollision();
	}

	// Raw-space tile origin, scaled to UE space at this placement boundary;
	// the baked vertices are local to it.
	MeshComponent->SetRelativeLocation(SWGToUnrealSpace(TileOrigin(Coord)));
	return MeshComponent;
}

FSWGTerrainTileBuild USWGTerrainSubsystem::BakeTerrainTile(const FSWGTerrainBakeSource& Source, const FIntPoint& Coord) const
{
	using namespace UE::Geometry;

	const FVector RegionOrigin = TileOrigin(Coord);

	FSWGTerrainTileBuild Build;
	Build.Heightmap = BakeHeightmap(Source, RegionOrigin);
	BakeShaderWeights(Source, Build.Heightmap);

	// Dynamic-mesh UVs reach the GPU in a compact representation. Keeping
	// absolute SWG world coordinates here loses fractional precision once a
	// player is far from (0,0), turning detail textures into noisy mips.
	// Rebase within this tile, but retain the tile origin's modulo so adjacent
	// tiles still meet at the same world-space texture phase.
	const FVector2f TerrainUVOrigin(
		FMath::Fmod(Build.Heightmap.Origin.X, TerrainTextureRepeatWorldSize) / TerrainTextureRepeatWorldSize,
		FMath::Fmod(Build.Heightmap.Origin.Y, TerrainTextureRepeatWorldSize) / TerrainTextureRepeatWorldSize);

	// Only this tile's holes, so the per-quad tests stay proportional to what is
	// near the tile rather than to every building in the zone.
	const FBox2D Bounds = GetTileBounds(Build.Heightmap);
	const TArray<FSWGTerrainHole> TileHoles = Source.Holes.FilterByPredicate(
		[&Bounds](const FSWGTerrainHole& Hole) { return Hole.GetWorldBounds().Intersect(Bounds); });

	// Vertices local to the tile's own component (see AcquireTileComponent).
	Build.Mesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
	BuildTerrainTileGeometry(*Build.Mesh, Build.Heightmap, HeightmapResolution, FVector::ZeroVector, TerrainUVOrigin,
		TileHoles, TerrainQuadSubdivisions);

	return Build;
}

void USWGTerrainSubsystem::ApplyTerrainTileBuild(const FIntPoint& Coord, FSWGTerrainTile& Tile, FSWGTerrainTileBuild& Build)
{
	check(IsInGameThread());

	if (!Build.Mesh.IsValid())
	{
		return;
	}

	if (!Tile.Component)
	{
		Tile.Component = AcquireTileComponent(Coord);
		if (!Tile.Component)
		{
			return;
		}
	}

	Tile.Heightmap = Build.Heightmap;

	// The whole point of the split: the triangulation already exists, so this is
	// a move rather than a per-vertex rebuild.
	Tile.Component->SetMesh(MoveTemp(*Build.Mesh));
	Tile.Component->SetMaterial(0, BuildTerrainTileMaterial(Build.Heightmap));

	// Shape changed, so the cooked collision is stale. bOnlyIfPending=false since
	// the flags haven't changed; bUseAsyncCooking keeps the cook off this thread.
	Tile.Component->UpdateCollision(false);
}
