#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGTerrainEvaluator.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeDataAccess.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"

namespace
{
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
			FColor P = LandscapeDataAccess::PackHeight(LandscapeDataAccess::GetTexHeight(Heights[i]));
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
}

void USWGTerrainSubsystem::Deinitialize()
{
}

void USWGTerrainSubsystem::BeginLoadTerrain(const FString TerrainVirtualPath, const FVector& SpawnPosition)
{
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

	TArray<FSWGBakedHeightmap> Grid;
	Grid.SetNum(ComponentGridSize * ComponentGridSize);

	for (int32 GridY = 0; GridY < ComponentGridSize; ++GridY)
	{
		for (int32 GridX = 0; GridX < ComponentGridSize; ++GridX)
		{
			const FVector RegionOrigin(GridOrigin.X + GridX * ComponentExtent, GridOrigin.Y + GridY * ComponentExtent, 0.0f);
			FSWGBakedHeightmap& Heightmap = Grid[GridY * ComponentGridSize + GridX];

			if (!FindCachedHeightmap(TerrainVirtualPath, RegionOrigin, Heightmap))
			{
				Heightmap = BakeHeightmap(TerrainData, RegionOrigin);
			}
		}
	}

	// SpawnLandscapeGrid touches actors/components/textures — all game-thread-only
	// — but LoadTerrain itself runs on the background thread BeginLoadTerrain
	// dispatched onto. Marshal back before touching any of that.
	AsyncTask(ENamedThreads::GameThread, [this, Grid = MoveTemp(Grid), GridOrigin, Spacing]()
		{
			SpawnLandscapeGrid(Grid, GridOrigin, Spacing);
			OnTerrainReady.Broadcast();
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

FSWGBakedHeightmap USWGTerrainSubsystem::BakeHeightmap(const FSWGTerrainData& TerrainData, const FVector& RegionOrigin)
{
	const int32 Resolution = HeightmapResolution;
	const float Spacing = HeightmapWorldExtent / (Resolution - 1);

	FSWGBakedHeightmap Heightmap;
	Heightmap.Origin = RegionOrigin;
	Heightmap.Spacing = Spacing;
	Heightmap.Heights.SetNumUninitialized(Resolution * Resolution);

	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		const float WorldY = RegionOrigin.Y + Row * Spacing;

		for (int32 Col = 0; Col < Resolution; ++Col)
		{
			const float WorldX = RegionOrigin.X + Col * Spacing;
			Heightmap.Heights[Row * Resolution + Col] = FSWGTerrainEvaluator::GetHeight(TerrainData, WorldX, WorldY);
		}
	}

	return Heightmap;
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

	// ALandscape's root component defaults to Static mobility — spawn it with the
	// transform already set, rather than calling SetActorLocation/SetActorScale3D
	// afterward, which logs a "has to be Movable" warning for a static root.
	// XY: each quad spans Spacing world units. Z: 1.0, so packed heights (via
	// LandscapeDataAccess::GetTexHeight, the exact inverse of GetLocalHeight)
	// reconstruct directly in our own world-height units with no extra scaling.
	//
	// ALandscapeProxy's constructor unconditionally does
	// RootComponent->SetRelativeScale3D(FVector(128, 128, 256)) — "Old default
	// scale, preserved for compatibility" (Landscape.cpp:1762) — and empirically
	// (confirmed via a live actor showing Scale=(2048,2048,256) for an intended
	// (16,16,1)) this composes multiplicatively with whatever scale we hand
	// SpawnActor, rather than being replaced by it. Pre-divide by that same
	// constant so the composed result comes out as intended.
	const FVector LandscapeConstructorDefaultScale(128.0f, 128.0f, 256.0f);
	const FVector DesiredScale(Spacing, Spacing, 1.0f);
	const FVector CompensatedScale = DesiredScale / LandscapeConstructorDefaultScale;
	const FTransform SpawnTransform(FQuat::Identity, GridOrigin, CompensatedScale);
	ALandscape* Landscape = Cast<ALandscape>(World->SpawnActor(ALandscape::StaticClass(), &SpawnTransform));
	if (!Landscape)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to spawn ALandscape"));
		return nullptr;
	}

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

	// Mip 0 goes through CreateTransient (matches its own internal construction of
	// mip 0 exactly); additional mips are appended the same way CreateTransient
	// builds its own — new FTexture2DMipMap + BulkData lock/realloc/unlock, none
	// of it editor-gated (Texture2D.cpp:1341-1349).
	//
	// Name must be unique per component — CreateTransient's NewObject<UTexture2D>
	// call used the same literal "SWGTerrainHeightmap" name for every component in
	// the grid, so every call after the first resolved to (and overwrote) the same
	// transient object instead of creating an independent one, leaving every
	// component but the last-baked one sampling the wrong texture.
	const TArray<FColor> Mip0Pixels = PackHeightMip(MipHeights[0], DefaultNormal);
	const FName TextureName(*FString::Printf(TEXT("SWGTerrainHeightmap_%d_%d"), SectionBase.X, SectionBase.Y));
	UTexture2D* HeightmapTexture = UTexture2D::CreateTransient(
		ComponentVerts, ComponentVerts, PF_B8G8R8A8, TextureName,
		TConstArrayView64<uint8>(reinterpret_cast<const uint8*>(Mip0Pixels.GetData()), Mip0Pixels.Num() * sizeof(FColor)));

	if (!HeightmapTexture)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGTerrainSubsystem: failed to create heightmap texture"));
		return;
	}

#if WITH_EDITOR
	// Editor-only: ULandscapeTextureHash::GetHash (LandscapeTextureHash.cpp:339-348,
	// itself entirely #if WITH_EDITORONLY_DATA) falls back to Source.GetId() whenever
	// no ULandscapeTextureHash asset user data is attached — which ours never has —
	// and asserts if Source is unpopulated. CreateTransient only builds PlatformData
	// (the runtime mips), never Source, so this always fires in-editor/PIE. Harmless
	// to skip in a packaged build: the whole check compiles away with WITH_EDITORONLY_DATA.
	// Only mip 0 is ever actually hashed (CalculateTextureHash64 only reads mip 0), so
	// that's all Source needs.
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
