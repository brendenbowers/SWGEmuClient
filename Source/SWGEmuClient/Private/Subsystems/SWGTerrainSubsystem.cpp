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
#include "Engine/StaticMeshActor.h"
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

	static FAutoConsoleCommand TraceHeightCmd(
		TEXT("swg.TraceHeight"),
		TEXT("swg.TraceHeight <x> <y> — logs every layer that changes height at that coordinate."),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
			{
				if (Args.Num() < 2)
				{
					UE_LOG(LogTemp, Warning, TEXT("Usage: swg.TraceHeight <x> <y>"));
					return;
				}

				const float X = FCString::Atof(*Args[0]);
				const float Y = FCString::Atof(*Args[1]);

				FSWGTerrainEvaluator::SetDebugTraceTarget(X, Y, true);
				const float Z = GetHeightAt(X, Y);
				FSWGTerrainEvaluator::SetDebugTraceTarget(0.0f, 0.0f, false);

				UE_LOG(LogTemp, Warning, TEXT("HEIGHTTRACE final x=%.2f y=%.2f height=%.4f"), X, Y, Z);
			}));

	// Diagnostic: isolates whether the runtime DDS->UTexture2D bridge produces a
	// renderable texture, decoupled from mesh-generator/shader-parsing logic —
	// loads one texture and displays it on a plane in front of the player.
	static FAutoConsoleCommand TestDDSTextureCmd(
		TEXT("swg.TestDDSTexture"),
		TEXT("swg.TestDDSTexture <texture virtual path> — loads a .dds directly and displays it on a plane in front of the player."),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogTemp, Warning, TEXT("Usage: swg.TestDDSTexture <texture virtual path>"));
					return;
				}

				if (!TreSubsystem || !TreSubsystem->FileExists(Args[0]))
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: %s not found in TRE"), *Args[0]);
					return;
				}

				const TArray<uint8> Bytes = TreSubsystem->ExtractFile(Args[0]);
				UTexture2D* Texture = FSWGDDSTextureLoader::LoadTexture2D(Bytes, FName(*Args[0]), /*bSRGB=*/true);
				if (!Texture)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: FSWGDDSTextureLoader failed for %s"), *Args[0]);
					return;
				}

				UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: %s decoded — SizeX=%d SizeY=%d PixelFormat=%d SRGB=%d HasResource=%d"),
					*Args[0], Texture->GetSizeX(), Texture->GetSizeY(), (int32)Texture->GetPixelFormat(),
					Texture->SRGB ? 1 : 0, Texture->GetResource() != nullptr ? 1 : 0);

				UWorld* World = GetWorld();
				APawn* Pawn = World ? World->GetFirstPlayerController()->GetPawn() : nullptr;
				if (!World || !Pawn)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: no world/pawn to spawn the test plane near"));
					return;
				}

				UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGObjectTextured.M_SWGObjectTextured"));
				if (!Parent)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: M_SWGObjectTextured not found"));
					return;
				}
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
				MID->SetTextureParameterValue(TEXT("Diffuse"), Texture);

				UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
				if (!PlaneMesh)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: /Engine/BasicShapes/Plane not found"));
					return;
				}

				const FVector SpawnLocation = Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 300.0f + FVector(0, 0, 100.0f);
				const FRotator SpawnRotation = (-Pawn->GetActorForwardVector()).Rotation() + FRotator(90.0f, 0.0f, 0.0f);

				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AStaticMeshActor* PlaneActor = World->SpawnActor<AStaticMeshActor>(SpawnLocation, SpawnRotation, SpawnParams);
				if (!PlaneActor)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: failed to spawn test plane actor"));
					return;
				}

				UStaticMeshComponent* MeshComponent = PlaneActor->GetStaticMeshComponent();
				MeshComponent->SetMobility(EComponentMobility::Movable);
				MeshComponent->SetStaticMesh(PlaneMesh);
				MeshComponent->SetWorldScale3D(FVector(3.0f, 3.0f, 1.0f));
				MeshComponent->SetMaterial(0, MID);

				UE_LOG(LogTemp, Warning, TEXT("swg.TestDDSTexture: spawned test plane at %s"), *SpawnLocation.ToString());
			}));
}

void USWGTerrainSubsystem::Deinitialize()
{
}

void USWGTerrainSubsystem::BeginLoadTerrain(const FString TerrainVirtualPath, const FVector& SpawnPosition)
{
	// Dropped here rather than at grid spawn: buildings register holes before the
	// grid exists, so clearing there would discard the ones from this load.
	TerrainHoles.Reset();

	// Likewise pads queued by the previous scene's buildings, and the previous
	// zone's height function — GetHeightAt must not answer for the old planet
	// while the new one bakes.
	PendingObjectModifications.Reset();
	bTerrainDataCached = false;


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

	FSWGTerrainData TerrainData;
	if (!ParseTerrain(TerrainVirtualPath, TerrainData))
	{
		Error(FString::Printf(TEXT("Failed to parse terrain: %s"), *TerrainVirtualPath));
		return;
	}

	const int32 ComponentVerts = HeightmapResolution;
	const float ComponentExtent = HeightmapWorldExtent; // world size of one component
	const float Spacing = ComponentExtent / (ComponentVerts - 1);
	const float GridExtent = ComponentExtent * ComponentGridSize;

	// Grid's min corner, not its center — SpawnPosition sits in the middle of the whole grid.
	const FVector GridOrigin(SpawnPosition.X - GridExtent * 0.5f, SpawnPosition.Y - GridExtent * 0.5f, 0.0f);

	const int32 TileCount = ComponentGridSize * ComponentGridSize;
	TArray<FSWGTerrainTileBuild> Grid;
	Grid.SetNum(TileCount);

	// This bake cannot see holes: buildings register them on the game thread
	// while this worker runs. FlushPendingTerrainHoles re-bakes for them below.
	const TArray<FSWGTerrainHole> Holes;

	// Tiles are independent by construction: GetHeight is a pure function of
	// world (x,y), so nothing here reads another tile's result.
	ParallelFor(TileCount, [this, &Grid, &TerrainData, &Holes, GridOrigin, ComponentExtent](int32 TileIndex)
		{
			const int32 GridX = TileIndex % ComponentGridSize;
			const int32 GridY = TileIndex / ComponentGridSize;
			const FVector RegionOrigin(GridOrigin.X + GridX * ComponentExtent, GridOrigin.Y + GridY * ComponentExtent, 0.0f);

			Grid[TileIndex] = BakeTerrainTile(TerrainData, RegionOrigin, GridOrigin, Holes);
		});

	TArray<FSWGWorldSnapshotSpawnInfo> SnapshotObjects = LoadWorldSnapshotObjects(TerrainVirtualPath, SpawnPosition);

	// SpawnLandscapeGrid touches actors/components/textures — all game-thread-only
	// — but LoadTerrain itself runs on the background thread BeginLoadTerrain
	// dispatched onto. Marshal back before touching any of that. Caching
	// TerrainData here too (rather than back on the background thread) avoids a
	// write/read race with GetHeightAt, which is only ever called from the game thread.
	AsyncTask(ENamedThreads::GameThread, [this, TerrainVirtualPath, Grid = MoveTemp(Grid), GridOrigin, Spacing, TerrainData = MoveTemp(TerrainData), SnapshotObjects = MoveTemp(SnapshotObjects)]() mutable
		{
			CachedTerrainData = TerrainData;
			bTerrainDataCached = true;
			ActiveTerrainVirtualPath = TerrainVirtualPath;

			SetupPlanetLighting(TerrainVirtualPath);
			SpawnDynamicMeshTerrainGrid(Grid, GridOrigin, Spacing);
			SpawnWorldSnapshotObjects(SnapshotObjects);
			// Tiles now exist and CachedTerrainData is populated, so anything
			// that spawned during the load can finally have its pad applied and
			// its rooms cut out.
			FlushPendingObjectTerrainModifications();
			FlushPendingTerrainHoles();
			OnTerrainReady.Broadcast();
		});
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
	if (Sun)
	{
		Sun->SetActorRotation(FRotator(-38.0f, -35.0f, 0.0f));
		UDirectionalLightComponent* SunComponent = Sun->GetComponent();
		SunComponent->SetMobility(EComponentMobility::Movable);
		SunComponent->SetAtmosphereSunLight(true);
		SunComponent->SetUseTemperature(true);
		SunComponent->SetTemperature(5600.0f);
		SunComponent->SetIntensity(3.0f);
	}

	ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (SkyLight)
	{
		SkyLight->Tags.Add(PlanetLightingTag);
		USkyLightComponent* SkyLightComponent = SkyLight->GetLightComponent();
		SkyLightComponent->SetMobility(EComponentMobility::Movable);
		SkyLightComponent->SetIntensity(1.0f);
		SkyLightComponent->SetRealTimeCaptureEnabled(true);
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
		Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(0.60f, 0.72f, 0.78f));
	}

	// TODO: fix this so it determines based on what is in the trn/ws file
	const FString ZoneName = FPaths::GetBaseFilename(TerrainVirtualPath).ToLower();
	const FString GradientPath = ZoneName == TEXT("naboo")
		? TEXT("texture/grad_sky_nboo.dds")
		: FString::Printf(TEXT("texture/grad_sky_%s.dds"), *ZoneName.Left(4));
	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: %s outdoor lighting active (SWG gradient source: %s; server sun direction not currently present in scene messages)"),
		*ZoneName, *GradientPath);
}

TArray<FSWGWorldSnapshotSpawnInfo> USWGTerrainSubsystem::LoadWorldSnapshotObjects(const FString& TerrainVirtualPath, const FVector& SpawnPosition)
{
	TArray<FSWGWorldSnapshotSpawnInfo> Result;

	// "terrain/tatooine.trn" -> "tatooine" -> "snapshot/tatooine.ws".
	FString ZoneName = FPaths::GetBaseFilename(TerrainVirtualPath);
	const FString SnapshotPath = FString::Printf(TEXT("snapshot/%s.ws"), *ZoneName);

	FSWGIffReader SnapshotReader = TreSubsystem->CreateIffReader(SnapshotPath);
	if (!SnapshotReader.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: no world snapshot found at %s — no static world objects will spawn"), *SnapshotPath);
		return Result;
	}

	FSWGWorldSnapshotData SnapshotData;
	if (!FSWGWorldSnapshotReader::ReadWorldSnapshot(SnapshotReader, SnapshotData))
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to parse world snapshot %s"), *SnapshotPath);
		return Result;
	}

	if (!FormTagMappingTable)
	{
		// Loadable off the game thread — SWGInitializationState's own CRC map
		// generation already does exactly this from a background ThreadPool task.
		FormTagMappingTable = LoadObject<UDataTable>(nullptr,
			TEXT("/Game/SWGEmu/Data/DT_SWGFormTagMappings.DT_SWGFormTagMappings"));
	}

	const float RadiusSq = WorldSnapshotSpawnRadius * WorldSnapshotSpawnRadius;
	int32 InRangeCount = 0;

	for (const FSWGWorldSnapshotNode& Node : SnapshotData.Nodes)
	{
		if (FVector::DistSquared(Node.Position, SpawnPosition) > RadiusSq)
			continue;

		++InRangeCount;

		FSWGWorldSnapshotSpawnInfo Info;
		if (ResolveWorldSnapshotNode(Node, SnapshotData, Info))
		{
			Result.Add(MoveTemp(Info));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: world snapshot %s — %d/%d node(s) within %.0f units of spawn, %d resolved to an actor class"),
		*SnapshotPath, InRangeCount, SnapshotData.Nodes.Num(), WorldSnapshotSpawnRadius, Result.Num());

	return Result;
}

bool USWGTerrainSubsystem::ResolveWorldSnapshotNode(const FSWGWorldSnapshotNode& Node, const FSWGWorldSnapshotData& SnapshotData, FSWGWorldSnapshotSpawnInfo& OutInfo) const
{
	if (!SnapshotData.ObjectTemplateNames.IsValidIndex((int32)Node.NameID))
		return false;

	const FString& TemplateName = SnapshotData.ObjectTemplateNames[(int32)Node.NameID];

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
		if (ResolveWorldSnapshotNode(ChildNode, SnapshotData, ChildInfo))
		{
			OutInfo.Children.Add(MoveTemp(ChildInfo));
		}
	}

	return true;
}

void USWGTerrainSubsystem::SpawnWorldSnapshotObjects(const TArray<FSWGWorldSnapshotSpawnInfo>& Objects)
{
	UWorld* World = GetWorld();
	if (!World)
		return;

	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;

	for (const FSWGWorldSnapshotSpawnInfo& Info : Objects)
	{
		// Info.Position is raw/native space (straight from the .ws file, compared
		// against WorldSnapshotSpawnRadius in that same raw space in
		// LoadWorldSnapshotObjects) — scale to final UE space right at this
		// actor-placement boundary.
		SpawnWorldSnapshotNode(Info, FTransform(Info.Rotation, SWGToUnrealSpace(Info.Position)), nullptr, ObjectGraph);
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: spawned %d world snapshot object(s)"), Objects.Num());
}

AActor* USWGTerrainSubsystem::SpawnWorldSnapshotNode(const FSWGWorldSnapshotSpawnInfo& Info, const FTransform& WorldTransform, AActor* Parent, USWGObjectGraphSubsystem* ObjectGraph, bool bForceInterior)
{
	UWorld* World = GetWorld();
	if (!World)
		return nullptr;

	ASWGBuilding* ParentBuilding = Cast<ASWGBuilding>(Parent);
	const bool bIsCell = Info.ActorClass->IsChildOf(ASWGCell::StaticClass());

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
		SpawnWorldSnapshotNode(Child, ChildRelative * WorldTransform, Actor, ObjectGraph, bForceInterior);
	}

	return Actor;
}

float USWGTerrainSubsystem::GetHeightAt(float X, float Y) const
{
	if (!bTerrainDataCached)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: GetHeightAt called before any terrain has parsed"));
		return 0.0f;
	}
	return FSWGTerrainEvaluator::GetHeight(CachedTerrainData, X, Y);
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
		ApplyObjectTerrainModification(Pending.TemplatePath, Pending.WorldPosition, Pending.YawRadians);
	}
}

bool USWGTerrainSubsystem::ApplyObjectTerrainModification(const FString& TemplatePath, const FVector& WorldPosition, float YawRadians)
{
	check(IsInGameThread());

	if (!bTerrainDataCached)
	{
		// Buildings reliably arrive before the async terrain load completes, so
		// this is the normal path on zone entry, not an edge case. Queue rather
		// than drop — dropping meant a building's pad simply never applied.
		PendingObjectModifications.Add({ TemplatePath, WorldPosition, YawRadians });
		UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: ApplyObjectTerrainModification(%s) before terrain finished loading — deferred (%d queued)"),
			*TemplatePath, PendingObjectModifications.Num());
		return true;
	}

	TArray<FSWGTerrainLayer> Layers;
	if (!BuildObjectTerrainLayers(TemplatePath, WorldPosition, YawRadians, Layers))
	{
		return false;
	}

	// Sample the height function before the layers go in, so the log below can
	// show what the flatten actually changed rather than what it intended to.
	const float CentreBefore = GetHeightAt((float)WorldPosition.X, (float)WorldPosition.Y);

	UE_LOG(LogTemp, Warning, TEXT("TERRAINPAD %s layers=%d padRawPos=(%.2f, %.2f, %.3f) existingLayers=%d heightAtCentreBefore=%.3f"),
		*TemplatePath, Layers.Num(), WorldPosition.X, WorldPosition.Y, WorldPosition.Z,
		CachedTerrainData.TopLevelLayers.Num(), CentreBefore);

	for (const FSWGTerrainLayer& Layer : Layers)
	{
		for (const FSWGTerrainBoundary& Boundary : Layer.Boundaries)
		{
			FString Corners;
			for (const FVector2D& Vertex : Boundary.Vertices)
			{
				Corners += FString::Printf(TEXT("(%.2f,%.2f) "), Vertex.X, Vertex.Y);
			}
			UE_LOG(LogTemp, Warning, TEXT("TERRAINPAD   boundary type=%d feather=%.3f verts=%d %s"),
				(int32)Boundary.Type, Boundary.FeatheringAmount, Boundary.Vertices.Num(), *Corners);
		}
		for (const FSWGTerrainAffector& Affector : Layer.Affectors)
		{
			UE_LOG(LogTemp, Warning, TEXT("TERRAINPAD   affector type=%d op=%d height=%.3f"),
				(int32)Affector.Type, Affector.OperationType, Affector.Height);
		}
	}

	ApplyTerrainLayersAndRegenerate(MoveTemp(Layers));

	// Re-sample the same points now the layers are live. Centre should equal the
	// affector height; the offsets show how far the pad actually reaches.
	const float CentreAfter = GetHeightAt((float)WorldPosition.X, (float)WorldPosition.Y);
	UE_LOG(LogTemp, Warning, TEXT("TERRAINPAD   heightAtCentreAfter=%.3f (delta %.3f)"), CentreAfter, CentreAfter - CentreBefore);

	for (const float Offset : { 5.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f })
	{
		const float HX = GetHeightAt((float)WorldPosition.X + Offset, (float)WorldPosition.Y);
		const float HY = GetHeightAt((float)WorldPosition.X, (float)WorldPosition.Y + Offset);
		UE_LOG(LogTemp, Warning, TEXT("TERRAINPAD   height at +%.0fm: X=%.3f Y=%.3f"), Offset, HX, HY);
	}

	return true;
}

void USWGTerrainSubsystem::ApplyTerrainLayersAndRegenerate(TArray<FSWGTerrainLayer> Layers)
{
	check(IsInGameThread());

	for (const FSWGTerrainLayer& Layer : Layers)
	{
		FBox2D LayerBounds;
		if (!FSWGTerrainModifier::GetLayerWorldBounds(Layer, LayerBounds))
		{
			// An unbounded layer would apply everywhere. Nothing in retail's
			// .lay/.sfp data is unbounded, so treat it as a data problem rather
			// than silently re-baking the entire grid.
			UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: terrain layer '%s' has no bounded region — skipping its tile invalidation"), *Layer.Name);
			continue;
		}

		InvalidateTilesOverlapping(LayerBounds);
	}

	if (bTerrainRegenerationInFlight)
	{
		// A worker is reading CachedTerrainData right now — appending would
		// reallocate under it. Hold the layers until that bake lands.
		QueuedTerrainLayers.Append(MoveTemp(Layers));
		return;
	}

	CachedTerrainData.TopLevelLayers.Append(MoveTemp(Layers));
	ProcessPendingTerrainRegeneration();
}

void USWGTerrainSubsystem::ProcessPendingTerrainRegeneration()
{
	check(IsInGameThread());

	if (bTerrainRegenerationInFlight || PendingDirtyTiles.IsEmpty())
	{
		return;
	}

	TArray<int32> TilesToBake = PendingDirtyTiles.Array();
	PendingDirtyTiles.Reset();
	bTerrainRegenerationInFlight = true;

	const int32 Generation = TerrainGeneration;
	TArray<FVector> Origins;
	Origins.Reserve(TilesToBake.Num());
	for (int32 TileIndex : TilesToBake)
	{
		Origins.Add(TerrainTileHeightmaps[TileIndex].Origin);
	}

	// Holes copied, not read off the member: the game thread keeps appending as
	// more buildings land.
	Async(EAsyncExecution::Thread, [this, Generation, TilesToBake = MoveTemp(TilesToBake), Origins = MoveTemp(Origins), Holes = TerrainHoles, GridOrigin = TerrainGridOrigin]() mutable
		{
			// Safe to read CachedTerrainData here: the game thread only ever
			// appends to it while bTerrainRegenerationInFlight is false.
			TArray<FSWGTerrainTileBuild> Rebaked;
			Rebaked.SetNum(Origins.Num());

			ParallelFor(Origins.Num(), [this, &Rebaked, &Origins, &Holes, GridOrigin](int32 i)
				{
					Rebaked[i] = BakeTerrainTile(CachedTerrainData, Origins[i], GridOrigin, Holes);
				});

			AsyncTask(ENamedThreads::GameThread, [this, Generation, TilesToBake = MoveTemp(TilesToBake), Rebaked = MoveTemp(Rebaked)]() mutable
				{
					bTerrainRegenerationInFlight = false;

					if (Generation != TerrainGeneration)
					{
						// Zone changed while this was baking — these tiles no
						// longer exist. Drop the result and the queue with it.
						QueuedTerrainLayers.Reset();
						return;
					}

					for (int32 i = 0; i < TilesToBake.Num(); ++i)
					{
						ApplyTerrainTileBuild(TilesToBake[i], Rebaked[i]);
					}

					UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: regenerated %d terrain tile(s) after a terrain modification"), TilesToBake.Num());

					if (QueuedTerrainLayers.Num() > 0)
					{
						CachedTerrainData.TopLevelLayers.Append(MoveTemp(QueuedTerrainLayers));
						QueuedTerrainLayers.Reset();
					}

					ProcessPendingTerrainRegeneration();
				});
		});
}

bool USWGTerrainSubsystem::FindCachedHeightmap(const FString& TerrainVirtualPath, const FVector& RegionOrigin, FSWGBakedHeightmap& OutHeightmap)
{
	return false;
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

FSWGBakedHeightmap USWGTerrainSubsystem::BakeHeightmap(const FSWGTerrainData& TerrainData, const FVector& RegionOrigin) const
{
	const int32 Resolution = HeightmapResolution;
	const float Spacing = HeightmapWorldExtent / (Resolution - 1);

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
			float Height = FSWGTerrainEvaluator::GetHeight(TerrainData, WorldX, WorldY);
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

	UE_LOG(LogTemp, Warning, TEXT("USWGTerrainSubsystem: BakeHeightmap region origin=(%.1f,%.1f) min=%.1f max=%.1f outOfRange(+/-%.0f)=%d/%d"),
		RegionOrigin.X, RegionOrigin.Y, MinHeight, MaxHeight, RepresentableHeightLimit, OutOfRangeCount, Resolution * Resolution);

	return Heightmap;
}

void USWGTerrainSubsystem::BakeShaderWeights(const FSWGTerrainData& TerrainData, FSWGBakedHeightmap& Heightmap) const
{
	const int32 Resolution = HeightmapResolution;
	const int32 SampleCount = Resolution * Resolution;

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
			FSWGTerrainEvaluator::GetShaderWeights(TerrainData, WorldX, WorldY, Weights);

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
		const FSWGShaderFamily* Family = CachedTerrainData.FindShaderFamily(Heightmap.ChosenShaderFamilyIds[Channel]);
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

void USWGTerrainSubsystem::SpawnLandscapeGrid(const TArray<FSWGBakedHeightmap>& Grid, const FVector& GridOrigin, float Spacing)
{
	check(IsInGameThread());

	ALandscape* Landscape = SpawnLandscapeActor(GridOrigin, Spacing);
	if (!Landscape)
	{
		return;
	}

	const int32 ComponentSizeQuads = HeightmapResolution - 1;

	for (int32 GridY = 0; GridY < ComponentGridSize; ++GridY)
	{
		for (int32 GridX = 0; GridX < ComponentGridSize; ++GridX)
		{
			const FSWGBakedHeightmap& Heightmap = Grid[GridY * ComponentGridSize + GridX];
			AddLandscapeComponent(Landscape, Heightmap, FIntPoint(GridX * ComponentSizeQuads, GridY * ComponentSizeQuads));
		}
	}

	FString Name = TEXT("UNKNOWN");
	if (ULevel* Level = Landscape->GetLevel())
	{
		Name = Level->GetName();
	}
	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: spawned %dx%d landscape grid in level: %s"), ComponentGridSize, ComponentGridSize, *Name);
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
			// raw/native space, matching the .trn's own units — SWGWorldScale
			// converts to final UE units right here, at the actual
			// vertex-placement boundary.
			const FVector3d Pos = FVector3d(
				LocalOrigin.X + Col * Heightmap.Spacing,
				LocalOrigin.Y + Row * Heightmap.Spacing,
				Height) * SWGWorldScale;

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

				if (!bTouchesHole)
				{
					AppendTriangle(GridSamples[I00], GridSamples[I01], GridSamples[I10]);
					AppendTriangle(GridSamples[I10], GridSamples[I01], GridSamples[I11]);
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

						AppendTriangle(Sub[S00], Sub[S01], Sub[S10]);
						AppendTriangle(Sub[S10], Sub[S01], Sub[S11]);
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

	// Holes change no heights, so this re-bake redoes work it needn't — but at
	// ~13 ms on a worker that buys one code path instead of two.
	InvalidateTilesOverlapping(Affected);
	ProcessPendingTerrainRegeneration();
}

void USWGTerrainSubsystem::FlushPendingTerrainHoles()
{
	check(IsInGameThread());

	if (TerrainHoles.IsEmpty())
	{
		return;
	}

	// Per hole rather than over their combined bounds, so two buildings at
	// opposite ends of the zone don't dirty everything between them.
	for (const FSWGTerrainHole& Hole : TerrainHoles)
	{
		InvalidateTilesOverlapping(Hole.GetWorldBounds());
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: applying %d terrain hole(s) registered during the terrain load (%d tile(s) dirtied)"),
		TerrainHoles.Num(), PendingDirtyTiles.Num());

	ProcessPendingTerrainRegeneration();
}

void USWGTerrainSubsystem::InvalidateTilesOverlapping(const FBox2D& Bounds)
{
	check(IsInGameThread());

	for (int32 TileIndex = 0; TileIndex < TerrainTileHeightmaps.Num(); ++TileIndex)
	{
		if (GetTileBounds(TerrainTileHeightmaps[TileIndex]).Intersect(Bounds))
		{
			PendingDirtyTiles.Add(TileIndex);
		}
	}
}

FSWGTerrainTileBuild USWGTerrainSubsystem::BakeTerrainTile(const FSWGTerrainData& TerrainData, const FVector& RegionOrigin,
	const FVector& GridOrigin, const TArray<FSWGTerrainHole>& Holes) const
{
	using namespace UE::Geometry;

	FSWGTerrainTileBuild Build;
	Build.Heightmap = BakeHeightmap(TerrainData, RegionOrigin);
	BakeShaderWeights(TerrainData, Build.Heightmap);

	// Relative to the actor (GridOrigin), same coordinate this tile's own
	// heights were baked in world-space against — no encoding/scale
	// indirection at all, just a direct offset.
	const FVector LocalOrigin = Build.Heightmap.Origin - GridOrigin;
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
	const FBox2D TileBounds = GetTileBounds(Build.Heightmap);
	const TArray<FSWGTerrainHole> TileHoles = Holes.FilterByPredicate(
		[&TileBounds](const FSWGTerrainHole& Hole) { return Hole.GetWorldBounds().Intersect(TileBounds); });

	Build.Mesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
	BuildTerrainTileGeometry(*Build.Mesh, Build.Heightmap, HeightmapResolution, LocalOrigin, TerrainUVOrigin,
		TileHoles, TerrainQuadSubdivisions);

	return Build;
}

void USWGTerrainSubsystem::ApplyTerrainTileBuild(int32 TileIndex, FSWGTerrainTileBuild& Build)
{
	check(IsInGameThread());

	if (!TerrainTileComponents.IsValidIndex(TileIndex) || !TerrainTileComponents[TileIndex] || !Build.Mesh.IsValid())
	{
		return;
	}

	UDynamicMeshComponent* MeshComponent = TerrainTileComponents[TileIndex];
	TerrainTileHeightmaps[TileIndex] = Build.Heightmap;

	// The whole point of the split: the triangulation already exists, so this is
	// a move rather than a per-vertex rebuild.
	MeshComponent->SetMesh(MoveTemp(*Build.Mesh));
	MeshComponent->SetMaterial(0, BuildTerrainTileMaterial(Build.Heightmap));

	// Shape changed, so the cooked collision is stale. bOnlyIfPending=false since
	// the flags haven't changed; bUseAsyncCooking keeps the cook off this thread.
	MeshComponent->UpdateCollision(false);
}

void USWGTerrainSubsystem::SpawnDynamicMeshTerrainGrid(TArray<FSWGTerrainTileBuild>& Grid, const FVector& GridOrigin, float Spacing)
{
	using namespace UE::Geometry;

	check(IsInGameThread());

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: no valid world to spawn terrain mesh in"));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* TerrainActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FQuat::Identity, GridOrigin), SpawnParams);
	if (!TerrainActor)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to spawn terrain mesh actor"));
		return;
	}

	// A plain AActor has no root component at construction time, so SpawnActor's
	// given transform (GridOrigin) has nothing to store itself in and silently
	// no-ops. Set the location explicitly once the root actually exists.
	USceneComponent* TerrainRoot = NewObject<USceneComponent>(TerrainActor, TEXT("TerrainRoot"));
	TerrainActor->SetRootComponent(TerrainRoot);
	TerrainRoot->RegisterComponent();
	// GridOrigin is raw/native space (matches the .trn's own units, same as
	// every position vertex placement below is computed against) — scale to
	// final UE space right at this actor-placement boundary.
	TerrainActor->SetActorLocation(SWGToUnrealSpace(GridOrigin));

	// Zone travel re-enters here with a fresh actor; drop the previous zone's
	// tile bookkeeping (and any regeneration queued against it) rather than
	// letting stale indices point at destroyed components.
	++TerrainGeneration;
	TerrainMeshActor = TerrainActor;
	TerrainGridOrigin = GridOrigin;
	TerrainTileComponents.Reset();
	TerrainTileHeightmaps.Reset();
	QueuedTerrainLayers.Reset();
	PendingDirtyTiles.Reset();

	const int32 Resolution = HeightmapResolution;

	for (int32 TileIndex = 0; TileIndex < Grid.Num(); ++TileIndex)
	{
		FSWGTerrainTileBuild& Build = Grid[TileIndex];
		if (Build.Heightmap.Heights.Num() != Resolution * Resolution || !Build.Mesh.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: terrain tile %d has %d samples (expected %d) or no mesh — skipping"),
				TileIndex, Build.Heightmap.Heights.Num(), Resolution * Resolution);
			continue;
		}

		UDynamicMeshComponent* MeshComponent = NewObject<UDynamicMeshComponent>(TerrainActor, NAME_None, RF_Transactional);
		MeshComponent->SetupAttachment(TerrainRoot);

		// Cooking collision synchronously is most of what made a tile cost
		// hundreds of ms of game thread, and nothing needs it the instant it appears.
		MeshComponent->bUseAsyncCooking = true;

		// Deliberately NOT calling SetColorOverrideMode(VertexColors) — any
		// non-None mode makes the scene proxy force-substitute the engine's
		// vertex-color debug material regardless of what's assigned. Vertex
		// color still uploads to the GPU with ColorMode at its default None,
		// which is all M_SWGTerrainBlend's VertexColor node needs.

		// Terrain was previously non-collidable at all (see world-object-plan.html
		// "Collision-data research pass") — every character fell forever through
		// empty space before its mesh loaded, since there was nothing to land on
		// (worked around, not fixed, by forcing MOVE_Flying until a real position
		// update arrived). Use the terrain's own baked triangle mesh directly as
		// its collision shape (complex-as-simple) — it's already a heightfield,
		// there's no cheaper "simple" approximation worth building separately.
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetCollisionObjectType(ECC_WorldStatic);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
		MeshComponent->RegisterComponent();
		MeshComponent->EnableComplexAsSimpleCollision();

		// Retained so ApplyObjectTerrainModification can re-bake and rewrite an
		// individual tile later without reloading the zone. Registered before
		// the mesh goes in, since ApplyTerrainTileBuild addresses tiles by index.
		TerrainTileComponents.Add(MeshComponent);
		TerrainTileHeightmaps.Add(Build.Heightmap);

		ApplyTerrainTileBuild(TerrainTileComponents.Num() - 1, Build);
	}

	UE_LOG(LogTemp, Log, TEXT("USWGTerrainSubsystem: spawned %d dynamic mesh terrain tile(s) at origin %s"), Grid.Num(), *GridOrigin.ToString());
}
