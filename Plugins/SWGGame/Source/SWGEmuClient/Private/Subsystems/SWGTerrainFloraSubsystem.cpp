#include "Subsystems/SWGTerrainFloraSubsystem.h"

#include "Common/SWGWorldScale.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "TRE/SWGMeshReader.h"
#include "Async/Async.h"
#include "HAL/IConsoleManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/UObjectIterator.h"

namespace
{
	TAutoConsoleVariable<int32> CVarFlora(
		TEXT("swg.Flora"), 1,
		TEXT("Master switch for procedural terrain vegetation (trees, clutter, grass billboards)."));

	TAutoConsoleVariable<int32> CVarFloraCollidable(TEXT("swg.FloraCollidable"), 1, TEXT("Collidable flora tier (trees, boulders)."));
	TAutoConsoleVariable<int32> CVarFloraNonCollidable(TEXT("swg.FloraNonCollidable"), 1, TEXT("Non-collidable flora tier (small shrubs, pebbles)."));
	TAutoConsoleVariable<int32> CVarFloraRadialNear(TEXT("swg.FloraRadialNear"), 1, TEXT("Near radial tier (grass/flower billboards)."));
	TAutoConsoleVariable<int32> CVarFloraRadialFar(TEXT("swg.FloraRadialFar"), 1, TEXT("Far radial tier (distant tree sprites)."));

	TAutoConsoleVariable<float> CVarFloraDensityScale(
		TEXT("swg.FloraDensityScale"), 1.0f,
		TEXT("Multiplies every flora family's density. 0 places nothing."));

	TAutoConsoleVariable<float> CVarFloraDistanceScale(
		TEXT("swg.FloraDistanceScale"), 1.0f,
		TEXT("Multiplies each tier's authored streaming distance (FSWGTerrainHeader)."));

	constexpr uint32 RadialBillboardMeshVersion = 1;

	/**
	 * Multiplied into every placement density. Calibrated by eye against the
	 * retail client on creature_test (2026-09-18): one candidate per
	 * authored tile at the layer's density reads ~25% denser than retail.
	 * Retail's exact per-tile pass/fail comes from its terrain generator's
	 * random stream and isn't reproducible from position data alone.
	 */
	constexpr float RetailDensityCalibration = 0.8f;

	const TCHAR* FloraTierName(ESWGTerrainFloraTier Tier)
	{
		switch (Tier)
		{
			case ESWGTerrainFloraTier::Collidable: return TEXT("collidable");
			case ESWGTerrainFloraTier::NonCollidable: return TEXT("non-collidable");
			case ESWGTerrainFloraTier::RadialNear: return TEXT("radial-near");
			default: return TEXT("radial-far");
		}
	}
}

void USWGTerrainFloraSubsystem::DumpStats() const
{
	if (!Planet)
	{
		UE_LOG(LogTemp, Warning, TEXT("swg.FloraStats: no planet loaded"));
		return;
	}

	FVector2D Center;
	const bool bHasCenter = GetStreamingCenter(Center);
	const USWGTerrainSubsystem::FSWGTerrainBakeSource Source = TerrainSubsystem->MakeBakeSource();
	const TArrayView<const FSWGTerrainLayer> ExtraLayers = Source.EditLayers ? MakeArrayView(*Source.EditLayers) : TArrayView<const FSWGTerrainLayer>();
	UE_LOG(LogTemp, Warning, TEXT("swg.FloraStats: centre raw (%.1f, %.1f), %d flora / %d radial families, %d cells, %d queued, %d in flight, %d appearances"),
		Center.X, Center.Y, Planet->FloraFamilies.Num(), Planet->RadialFamilies.Num(), Cells.Num(), PlaceQueue.Num(), PlacementsInFlight, Appearances.Num());

	for (int32 TierIndex = 0; TierIndex < (int32)ESWGTerrainFloraTier::Count; ++TierIndex)
	{
		const ESWGTerrainFloraTier Tier = (ESWGTerrainFloraTier)TierIndex;
		const FSWGTerrainHeader::FVegetationTier& TierParams = Planet->GetVegetationTier(Tier);
		int32 CellCount = 0, ComponentCount = 0, InstanceCount = 0, PendingCount = 0;
		for (const TPair<FCellKey, FCell>& Pair : Cells)
		{
			if (Pair.Key.Tier != Tier) continue;
			++CellCount;
			ComponentCount += Pair.Value.Components.Num();
			for (const UInstancedStaticMeshComponent* Component : Pair.Value.Components)
			{
				InstanceCount += Component ? Component->GetInstanceCount() : 0;
			}
			for (const FInstanceGroup& Group : Pair.Value.PendingGroups)
			{
				PendingCount += Group.Transforms.Num();
			}
		}

		FString SampleText = TEXT("no centre");
		if (bHasCenter)
		{
			const FSWGTerrainFloraSample Sample = FSWGTerrainEvaluator::GetFlora(*Planet, Center.X, Center.Y, Tier, ExtraLayers);
			SampleText = FString::Printf(TEXT("family %d density %.2f height %.1f"), Sample.FamilyId, Sample.Density, Sample.Height);
		}
		UE_LOG(LogTemp, Warning, TEXT("  %s: %s, range %.0f-%.0f m, tile %.1f m, cell %.0f m: %d cell(s), %d component(s), %d instance(s), %d pending; at centre: %s"),
			FloraTierName(Tier), IsTierEnabled(Tier) ? TEXT("on") : TEXT("off"), TierParams.MinDistance, TierParams.MaxDistance, TierParams.TileSize,
			FSWGTerrainFloraPlacer::GetCellSize(*Planet, Tier), CellCount, ComponentCount, InstanceCount, PendingCount, *SampleText);
	}

	for (const TPair<FString, FAppearanceMesh>& Pair : Appearances)
	{
		UE_LOG(LogTemp, Warning, TEXT("  appearance %s: %s"), *Pair.Key, Pair.Value.bFailed ? TEXT("FAILED") : Pair.Value.Mesh ? TEXT("ready") : TEXT("pending"));
	}
}

static FAutoConsoleCommand GSWGFloraStatsCommand(
	TEXT("swg.FloraStats"),
	TEXT("Logs the terrain flora streamer's per-tier cell/instance counts and the flora sample at the player."),
	FConsoleCommandDelegate::CreateLambda([]()
		{
			// Same lookup as swg.Command: the editor console's world isn't the PIE one.
			for (TObjectIterator<USWGTerrainFloraSubsystem> It; It; ++It)
			{
				if (IsValid(*It) && It->GetGameInstance())
				{
					It->DumpStats();
					return;
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("swg.FloraStats: no live flora subsystem — not in a session yet"));
		}));

void USWGTerrainFloraSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TerrainSubsystem = Collection.InitializeDependency<USWGTerrainSubsystem>();
	MeshGenerator = Collection.InitializeDependency<USWGMeshGeneratorSubsystem>();

	if (TerrainSubsystem)
	{
		TerrainSubsystem->OnTerrainLoaded.AddUObject(this, &USWGTerrainFloraSubsystem::OnTerrainLoaded);
		TerrainSubsystem->OnZoneReset.AddUObject(this, &USWGTerrainFloraSubsystem::OnZoneReset);
		TerrainSubsystem->OnTerrainEditsChanged.AddUObject(this, &USWGTerrainFloraSubsystem::OnTerrainEditsChanged);
	}
}

void USWGTerrainFloraSubsystem::Deinitialize()
{
	if (TerrainSubsystem)
	{
		TerrainSubsystem->OnTerrainLoaded.RemoveAll(this);
		TerrainSubsystem->OnZoneReset.RemoveAll(this);
		TerrainSubsystem->OnTerrainEditsChanged.RemoveAll(this);
	}
	ResetAll();
	Super::Deinitialize();
}

TStatId USWGTerrainFloraSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGTerrainFloraSubsystem, STATGROUP_Tickables);
}

void USWGTerrainFloraSubsystem::Tick(float DeltaTime)
{
	TimeUntilNextSweep -= DeltaTime;
	if (TimeUntilNextSweep <= 0.0f)
	{
		TimeUntilNextSweep = StreamingSweepInterval;
		UpdateStreaming();
	}
}

// ── Lifecycle ───────────────────────────────────────────────────────────

void USWGTerrainFloraSubsystem::OnTerrainLoaded()
{
	ResetAll();
	Planet = TerrainSubsystem ? TerrainSubsystem->MakeBakeSource().Planet : nullptr;
	if (!Planet)
	{
		return;
	}
	SpawnFloraActor();
	bTerrainLoaded = true;
	TimeUntilNextSweep = 0.0f;
}

void USWGTerrainFloraSubsystem::OnZoneReset()
{
	bTerrainLoaded = false;
	ResetAll();
}

void USWGTerrainFloraSubsystem::OnTerrainEditsChanged(const FBox2D& RawBounds)
{
	if (!bTerrainLoaded || !Planet)
	{
		return;
	}

	for (TPair<FCellKey, FCell>& Pair : Cells)
	{
		const float CellSize = FSWGTerrainFloraPlacer::GetCellSize(*Planet, Pair.Key.Tier);
		const FVector2D Min(Pair.Key.Coord.X * CellSize, Pair.Key.Coord.Y * CellSize);
		if (!FBox2D(Min, Min + FVector2D(CellSize, CellSize)).Intersect(RawBounds))
		{
			continue;
		}

		++Pair.Value.WantedEditVersion;
		if (!Pair.Value.bPlaceInFlight)
		{
			PlaceQueue.Add(Pair.Key);
		}
	}
	PumpPlaceQueue();
}

void USWGTerrainFloraSubsystem::SpawnFloraActor()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.Name = MakeUniqueObjectName(World, AActor::StaticClass(), TEXT("SWGTerrainFlora"));
	FloraActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!FloraActor)
	{
		return;
	}
#if WITH_EDITOR
	FloraActor->SetActorLabel(TEXT("SWGTerrainFlora"));
#endif
	USceneComponent* Root = NewObject<USceneComponent>(FloraActor, TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	FloraActor->SetRootComponent(Root);
	Root->RegisterComponent();
}

void USWGTerrainFloraSubsystem::ResetAll()
{
	++Generation;
	Cells.Reset();
	PlaceQueue.Reset();
	Appearances.Reset();
	// In-flight placements still count until they land and see the new generation.

	if (IsValid(FloraActor))
	{
		FloraActor->Destroy();
	}
	FloraActor = nullptr;
	RadialBillboardMesh = nullptr;
	Planet.Reset();
}

// ── Streaming ────────────────────────────────────────────────────────────

bool USWGTerrainFloraSubsystem::GetStreamingCenter(FVector2D& OutRawPosition) const
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}

	const FVector Raw = SWGToRawSpace(Pawn->GetActorLocation());
	OutRawPosition = FVector2D(Raw.X, Raw.Y);
	return true;
}

bool USWGTerrainFloraSubsystem::IsTierEnabled(ESWGTerrainFloraTier Tier)
{
	if (CVarFlora.GetValueOnGameThread() == 0)
	{
		return false;
	}
	switch (Tier)
	{
		case ESWGTerrainFloraTier::Collidable: return CVarFloraCollidable.GetValueOnGameThread() != 0;
		case ESWGTerrainFloraTier::NonCollidable: return CVarFloraNonCollidable.GetValueOnGameThread() != 0;
		case ESWGTerrainFloraTier::RadialNear: return CVarFloraRadialNear.GetValueOnGameThread() != 0;
		default: return CVarFloraRadialFar.GetValueOnGameThread() != 0;
	}
}

bool USWGTerrainFloraSubsystem::IsCellWanted(ESWGTerrainFloraTier Tier, const FIntPoint& Coord, const FVector2D& Center, bool bForUnload) const
{
	const FSWGTerrainHeader::FVegetationTier& TierParams = Planet->GetVegetationTier(Tier);
	const float DistanceScale = FMath::Max(0.0f, CVarFloraDistanceScale.GetValueOnGameThread());
	const float CellSize = FSWGTerrainFloraPlacer::GetCellSize(*Planet, Tier);
	// A cell is in the ring while any part of it can be — its centre within
	// half a diagonal of the annulus — plus one cell of slack on the way out.
	const float HalfDiagonal = CellSize * UE_HALF_SQRT_2 + (bForUnload ? CellSize : 0.0f);
	const float Distance = FVector2D::Distance(FSWGTerrainFloraPlacer::CellCenter(CellSize, Coord), Center);

	return Distance - HalfDiagonal <= TierParams.MaxDistance * DistanceScale
		&& Distance + HalfDiagonal >= TierParams.MinDistance * DistanceScale;
}

void USWGTerrainFloraSubsystem::UpdateStreaming()
{
	check(IsInGameThread());

	if (!bTerrainLoaded || !Planet || !IsValid(FloraActor))
	{
		return;
	}

	FVector2D Center;
	if (!GetStreamingCenter(Center))
	{
		return;
	}

	const float DistanceScale = FMath::Max(0.0f, CVarFloraDistanceScale.GetValueOnGameThread());

	for (int32 TierIndex = 0; TierIndex < (int32)ESWGTerrainFloraTier::Count; ++TierIndex)
	{
		const ESWGTerrainFloraTier Tier = (ESWGTerrainFloraTier)TierIndex;
		if (!IsTierEnabled(Tier))
		{
			continue;
		}

		const float CellSize = FSWGTerrainFloraPlacer::GetCellSize(*Planet, Tier);
		const float MaxDistance = Planet->GetVegetationTier(Tier).MaxDistance * DistanceScale;
		const int32 RadiusCells = FMath::CeilToInt(MaxDistance / CellSize) + 1;
		const FIntPoint CenterCell = FSWGTerrainFloraPlacer::CellCoordAt(CellSize, Center);

		for (int32 OffsetY = -RadiusCells; OffsetY <= RadiusCells; ++OffsetY)
		{
			for (int32 OffsetX = -RadiusCells; OffsetX <= RadiusCells; ++OffsetX)
			{
				const FCellKey Key{ Tier, CenterCell + FIntPoint(OffsetX, OffsetY) };
				if (Cells.Contains(Key) || !IsCellWanted(Tier, Key.Coord, Center, false))
				{
					continue;
				}
				Cells.Add(Key);
				PlaceQueue.Add(Key);
			}
		}
	}

	TArray<FCellKey> ToUnload;
	for (const TPair<FCellKey, FCell>& Pair : Cells)
	{
		if (!IsTierEnabled(Pair.Key.Tier) || !IsCellWanted(Pair.Key.Tier, Pair.Key.Coord, Center, true))
		{
			ToUnload.Add(Pair.Key);
		}
	}
	for (const FCellKey& Key : ToUnload)
	{
		UnloadCell(Key);
	}

	PumpPlaceQueue();
}

void USWGTerrainFloraSubsystem::PumpPlaceQueue()
{
	if (PlaceQueue.IsEmpty() || PlacementsInFlight >= MaxPlacementsInFlight || !Planet)
	{
		return;
	}

	FVector2D Center;
	if (!GetStreamingCenter(Center))
	{
		return;
	}

	TArray<FCellKey> Queued = PlaceQueue.Array();
	Queued.Sort([&](const FCellKey& Left, const FCellKey& Right)
		{
			const float LeftSize = FSWGTerrainFloraPlacer::GetCellSize(*Planet, Left.Tier);
			const float RightSize = FSWGTerrainFloraPlacer::GetCellSize(*Planet, Right.Tier);
			return FVector2D::DistSquared(FSWGTerrainFloraPlacer::CellCenter(LeftSize, Left.Coord), Center)
				< FVector2D::DistSquared(FSWGTerrainFloraPlacer::CellCenter(RightSize, Right.Coord), Center);
		});

	for (const FCellKey& Key : Queued)
	{
		if (PlacementsInFlight >= MaxPlacementsInFlight)
		{
			break;
		}
		PlaceQueue.Remove(Key);
		StartCellPlacement(Key);
	}
}

void USWGTerrainFloraSubsystem::StartCellPlacement(const FCellKey& Key)
{
	FCell* Cell = Cells.Find(Key);
	if (!Cell || Cell->bPlaceInFlight)
	{
		return;
	}

	const USWGTerrainSubsystem::FSWGTerrainBakeSource Source = TerrainSubsystem->MakeBakeSource();
	if (!Source.Planet)
	{
		return;
	}

	Cell->bPlaceInFlight = true;
	++PlacementsInFlight;

	const int32 JobGeneration = Generation;
	const int32 JobEditVersion = Cell->WantedEditVersion;
	const float DensityScale = FMath::Max(0.0f, CVarFloraDensityScale.GetValueOnGameThread()) * RetailDensityCalibration;

	Async(EAsyncExecution::ThreadPool, [this, Key, Source, JobGeneration, JobEditVersion, DensityScale]()
		{
			TArray<FSWGTerrainFloraInstance> Instances;
			const TArrayView<const FSWGTerrainLayer> ExtraLayers = Source.EditLayers ? MakeArrayView(*Source.EditLayers) : TArrayView<const FSWGTerrainLayer>();
			FSWGTerrainFloraPlacer::PlaceCell(*Source.Planet, ExtraLayers, Key.Tier, Key.Coord, DensityScale,
				[&Source](const FVector2D& RawPosition) { return Source.IsInHole(RawPosition); }, Instances);

			AsyncTask(ENamedThreads::GameThread, [this, Key, JobGeneration, JobEditVersion, Instances = MoveTemp(Instances)]() mutable
				{
					OnCellPlaced(Key, JobGeneration, JobEditVersion, MoveTemp(Instances));
				});
		});
}

void USWGTerrainFloraSubsystem::OnCellPlaced(const FCellKey& Key, int32 JobGeneration, int32 JobEditVersion, TArray<FSWGTerrainFloraInstance>&& Instances)
{
	check(IsInGameThread());
	--PlacementsInFlight;

	if (JobGeneration != Generation)
	{
		return;
	}

	FCell* Cell = Cells.Find(Key);
	if (!Cell)
	{
		PumpPlaceQueue();
		return;
	}
	Cell->bPlaceInFlight = false;
	Cell->PlacedEditVersion = JobEditVersion;

	// A re-place swaps the old components out for the new set in one go.
	for (UInstancedStaticMeshComponent* Component : Cell->Components)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	Cell->Components.Reset();
	Cell->PendingGroups.Reset();

	BuildInstanceGroups(Key.Tier, Instances, Cell->PendingGroups);
	FlushPendingGroups(Key, *Cell);

	if (Cell->WantedEditVersion != JobEditVersion)
	{
		PlaceQueue.Add(Key);
	}
	PumpPlaceQueue();
}

void USWGTerrainFloraSubsystem::UnloadCell(const FCellKey& Key)
{
	FCell Cell;
	if (!Cells.RemoveAndCopyValue(Key, Cell))
	{
		return;
	}
	PlaceQueue.Remove(Key);

	for (UInstancedStaticMeshComponent* Component : Cell.Components)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	// A placement in flight lands on a missing cell and is dropped there.
}

// ── Instancing ──────────────────────────────────────────────────────────

void USWGTerrainFloraSubsystem::BuildInstanceGroups(ESWGTerrainFloraTier Tier, const TArray<FSWGTerrainFloraInstance>& Instances, TArray<FInstanceGroup>& OutGroups) const
{
	if (!Planet)
	{
		return;
	}
	const bool bRadial = SWGIsRadialFloraTier(Tier);

	TMap<FString, int32> GroupIndexByKey;
	for (const FSWGTerrainFloraInstance& Instance : Instances)
	{
		FString AppearanceKey;
		if (bRadial)
		{
			const FSWGRadialFamily* Family = Planet->FindRadialFamily(Instance.FamilyId);
			if (!Family || !Family->Children.IsValidIndex(Instance.ChildIndex)) continue;
			AppearanceKey = Family->Children[Instance.ChildIndex].ShaderName;
		}
		else
		{
			const FSWGFloraFamily* Family = Planet->FindFloraFamily(Instance.FamilyId);
			if (!Family || !Family->Children.IsValidIndex(Instance.ChildIndex)) continue;
			const FString& AppearanceName = Family->Children[Instance.ChildIndex].AppearanceName;
			// Particle-effect children (swarming insects) have no mesh to instance.
			if (!AppearanceName.EndsWith(TEXT(".apt")) && !AppearanceName.EndsWith(TEXT(".lod")) && !AppearanceName.EndsWith(TEXT(".msh"))) continue;
			AppearanceKey = TEXT("appearance/") + AppearanceName;
		}

		// Raw yaw is counter-clockwise about up in the east/north plane; UE's
		// yaw is the opposite sense (see SWGToRawYawRadians). Alignment tilts
		// the instance's up onto the ground normal before the yaw.
		const FVector UpUE(Instance.RawNormal.Y, Instance.RawNormal.X, Instance.RawNormal.Z);
		const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, UpUE.GetSafeNormal(UE_KINDA_SMALL_NUMBER, FVector::UpVector));
		const FQuat Yaw(FVector::UpVector, -Instance.YawRadians);
		// A billboard's Scale is its width; CreateGroupComponent stretches Z to the texture's aspect.
		const FTransform Transform(Tilt * Yaw, SWGToUnrealSpace(Instance.RawPosition), FVector(Instance.Scale));

		int32* GroupIndex = GroupIndexByKey.Find(AppearanceKey);
		if (!GroupIndex)
		{
			FInstanceGroup& Group = OutGroups.AddDefaulted_GetRef();
			Group.AppearanceKey = AppearanceKey;
			Group.bRadial = bRadial;
			GroupIndex = &GroupIndexByKey.Add(AppearanceKey, OutGroups.Num() - 1);
		}
		OutGroups[*GroupIndex].Transforms.Add(Transform);
	}
}

void USWGTerrainFloraSubsystem::FlushPendingGroups(const FCellKey& Key, FCell& Cell)
{
	for (int32 GroupIndex = Cell.PendingGroups.Num() - 1; GroupIndex >= 0; --GroupIndex)
	{
		const FInstanceGroup& Group = Cell.PendingGroups[GroupIndex];
		FAppearanceMesh* Appearance = Appearances.Find(Group.AppearanceKey);
		if (!Appearance)
		{
			// Billboards resolve synchronously; meshes come back through OnAppearanceMeshReady.
			RequestAppearance(Group.AppearanceKey, Group.bRadial);
			Appearance = Appearances.Find(Group.AppearanceKey);
		}
		if (!Appearance || (!Appearance->Mesh && !Appearance->bFailed))
		{
			continue;
		}
		if (Appearance->bFailed)
		{
			Cell.PendingGroups.RemoveAtSwap(GroupIndex);
			continue;
		}

		if (UInstancedStaticMeshComponent* Component = CreateGroupComponent(Key.Tier, Group, *Appearance))
		{
			Cell.Components.Add(Component);
		}
		Cell.PendingGroups.RemoveAtSwap(GroupIndex);
	}
}

void USWGTerrainFloraSubsystem::RequestAppearance(const FString& AppearanceKey, bool bRadial)
{
	FAppearanceMesh& Appearance = Appearances.Add(AppearanceKey);
	Appearance.bRequested = true;

	if (bRadial)
	{
		// The billboard mesh is shared; only the shader's material is per key.
		UStaticMesh* Mesh = GetOrBuildRadialBillboardMesh();
		UMaterialInterface* Material = MeshGenerator ? MeshGenerator->GetOrBuildObjectMaterial(FString::Printf(TEXT("shader/%s.sht"), *AppearanceKey)) : nullptr;
		TArray<UMaterialInterface*> Materials;
		if (Material)
		{
			Materials.Add(Material);
		}
		ResolveAppearance(Appearance, AppearanceKey, Mesh, Materials);
		return;
	}

	if (!MeshGenerator)
	{
		Appearance.bFailed = true;
		return;
	}

	const int32 RequestGeneration = Generation;
	MeshGenerator->RequestAppearanceMesh(AppearanceKey, [this, AppearanceKey, RequestGeneration](UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
		{
			if (RequestGeneration == Generation)
			{
				OnAppearanceMeshReady(AppearanceKey, Mesh, Materials);
			}
		});
}

void USWGTerrainFloraSubsystem::ResolveAppearance(FAppearanceMesh& Appearance, const FString& AppearanceKey, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
{
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGTerrainFloraSubsystem: no mesh for flora appearance '%s' — its instances are dropped"), *AppearanceKey);
		Appearance.bFailed = true;
		return;
	}

	Appearance.Mesh = Mesh;
	Appearance.Materials.Reset();
	for (UMaterialInterface* Material : Materials)
	{
		Appearance.Materials.Add(Material);
	}

	// A billboard's height comes from its texture's aspect ratio.
	UTexture* Diffuse = nullptr;
	UMaterialInstanceDynamic* Dynamic = Materials.Num() > 0 ? Cast<UMaterialInstanceDynamic>(Materials[0]) : nullptr;
	if (Dynamic && Dynamic->GetTextureParameterValue(FName(TEXT("Diffuse")), Diffuse) && Diffuse)
	{
		const float Width = Diffuse->GetSurfaceWidth();
		const float Height = Diffuse->GetSurfaceHeight();
		if (Width > 0.0f && Height > 0.0f)
		{
			Appearance.HeightPerWidth = Height / Width;
		}
	}
}

void USWGTerrainFloraSubsystem::OnAppearanceMeshReady(const FString& AppearanceKey, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
{
	check(IsInGameThread());

	FAppearanceMesh* Appearance = Appearances.Find(AppearanceKey);
	if (!Appearance)
	{
		return;
	}
	ResolveAppearance(*Appearance, AppearanceKey, Mesh, Materials);

	for (TPair<FCellKey, FCell>& Pair : Cells)
	{
		if (Pair.Value.PendingGroups.Num() > 0)
		{
			FlushPendingGroups(Pair.Key, Pair.Value);
		}
	}
}

UInstancedStaticMeshComponent* USWGTerrainFloraSubsystem::CreateGroupComponent(ESWGTerrainFloraTier Tier, const FInstanceGroup& Group, const FAppearanceMesh& Appearance)
{
	if (!IsValid(FloraActor) || !Appearance.Mesh || Group.Transforms.IsEmpty())
	{
		return nullptr;
	}

	const float MaxDistance = Planet ? Planet->GetVegetationTier(Tier).MaxDistance : 128.0f;
	const float DistanceScale = FMath::Max(0.0f, CVarFloraDistanceScale.GetValueOnGameThread());

	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(FloraActor);
	Component->SetMobility(EComponentMobility::Static);
	Component->SetStaticMesh(Appearance.Mesh);
	for (int32 SlotIndex = 0; SlotIndex < Appearance.Materials.Num(); ++SlotIndex)
	{
		if (Appearance.Materials[SlotIndex])
		{
			Component->SetMaterial(SlotIndex, Appearance.Materials[SlotIndex]);
		}
	}
	// The generated meshes carry no collision geometry yet; walking through a
	// tree is the known gap here, not something a collision channel fixes.
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(!Group.bRadial);
	Component->SetCullDistances(0, FMath::CeilToInt(SWGToUnrealSpace(MaxDistance * DistanceScale) * 1.1f));
	Component->SetupAttachment(FloraActor->GetRootComponent());
	Component->RegisterComponent();

	if (Group.bRadial)
	{
		// Width is uniform in the placed transform; stretch to the texture's aspect.
		TArray<FTransform> Stretched = Group.Transforms;
		for (FTransform& Transform : Stretched)
		{
			FVector Scale = Transform.GetScale3D();
			Scale.Z *= Appearance.HeightPerWidth;
			Transform.SetScale3D(Scale);
		}
		Component->AddInstances(Stretched, false, true);
	}
	else
	{
		Component->AddInstances(Group.Transforms, false, true);
	}

	return Component;
}

UStaticMesh* USWGTerrainFloraSubsystem::GetOrBuildRadialBillboardMesh()
{
	if (RadialBillboardMesh)
	{
		return RadialBillboardMesh;
	}
	if (!MeshGenerator)
	{
		return nullptr;
	}

	// Two 1 m quads (UE cm) crossed on the up axis, each with a mirrored
	// twin so the back faces draw. V runs top-down like the .dds.
	FSWGMeshData MeshData;
	FSWGMeshSubmesh& Submesh = MeshData.Submeshes.AddDefaulted_GetRef();
	Submesh.ShaderName = TEXT("radial_billboard");

	// Right runs along the quad; its geometric normal is up x right, and the
	// mirrored twin (negated Right) reverses the winding for the other side.
	auto AddQuad = [&Submesh](const FVector& Right)
		{
			const FVector Normal = FVector::CrossProduct(FVector::UpVector, Right);
			const int32 FirstVertex = Submesh.Vertices.Num();
			const FVector Corners[4] = { -Right * 50.0f, Right * 50.0f, Right * 50.0f + FVector(0, 0, 100.0f), -Right * 50.0f + FVector(0, 0, 100.0f) };
			const FVector2D UVs[4] = { FVector2D(0.0f, 1.0f), FVector2D(1.0f, 1.0f), FVector2D(1.0f, 0.0f), FVector2D(0.0f, 0.0f) };
			for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
			{
				FSWGMeshVertex& Vertex = Submesh.Vertices.AddDefaulted_GetRef();
				Vertex.Position = Corners[CornerIndex];
				Vertex.Normal = Normal;
				Vertex.UVs = { UVs[CornerIndex] };
			}
			Submesh.Triangles.Append({ FirstVertex, FirstVertex + 2, FirstVertex + 1, FirstVertex, FirstVertex + 3, FirstVertex + 2 });
		};
	AddQuad(FVector::YAxisVector);
	AddQuad(-FVector::YAxisVector);
	AddQuad(FVector::XAxisVector);
	AddQuad(-FVector::XAxisVector);
	MeshData.BoundingBox = FBox(FVector(-50.0f, -50.0f, 0.0f), FVector(50.0f, 50.0f, 100.0f));
	MeshData.bHasBoundingBox = true;

	const uint32 CacheHash = HashCombine(GetTypeHash(FString(TEXT("SWGRadialFloraBillboard"))), RadialBillboardMeshVersion);
	RadialBillboardMesh = MeshGenerator->GetOrBuildGeneratedStaticMesh(CacheHash, TEXT("radial flora billboard"), MeshData, FVector3f(0.3f, 0.6f, 0.2f));
	return RadialBillboardMesh;
}
