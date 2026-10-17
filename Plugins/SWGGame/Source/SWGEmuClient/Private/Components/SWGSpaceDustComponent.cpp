#include "Components/SWGSpaceDustComponent.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Objects/World/SWGShip.h"
#include "TRE/SWGSpaceTerrainReader.h"
#include "TRE/SWGDataTableReader.h"
#include "Common/SWGWorldScale.h"
#include "Common/SWGSpaceQuality.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"

namespace
{
	TAutoConsoleVariable<int32> CVarSpaceDebris(
		TEXT("swg.Space.Debris"), 1,
		TEXT("Tumbling asteroid debris around the camera inside the zone's asteroid fields (0/1). Next zone load for the meshes; the layer toggles live."));

	TAutoConsoleVariable<float> CVarSpaceDustCountScale(
		TEXT("swg.Space.DustCountScale"), 1.0f,
		TEXT("Fraction 0-1 of the zone's retail dust count that is drawn; swg.Space.Quality sets it. Next zone load."));

	// Retail's radial asteroid style (datatables/space/asteroidfield/fieldstyle/radial_s01) names asteroid_radial_s01-s03; these are their lowest LODs.
	const TCHAR* const DebrisMeshPaths[] = {
		TEXT("appearance/mesh/asteroid_radial_s01_l2.msh"), TEXT("appearance/mesh/asteroid_radial_s02_l2.msh"), TEXT("appearance/mesh/asteroid_radial_s03_l2.msh")};
	constexpr int32 DebrisMeshCount = UE_ARRAY_COUNT(DebrisMeshPaths);
	// ShipObject baseline 1 scalar 17+n: boosterMaxSpeed.
	constexpr int32 BoosterMaxSpeedIndex = 18;
	constexpr double DebrisMaxDrift = 200.0;
	// Hull bounds grow by this much so a rock never clips the ship.
	constexpr double DebrisHullMargin = 500.0;

	// Native units are x east, y up, z north; the UE space frame is x forward (native z), y right (native x), z up.
	FVector NativeToSpace(const FVector& Native) { return FVector(Native.Z, Native.X, Native.Y)*SWGWorldScale; }

	FBox GetShipBounds(const USWGObjectGraphSubsystem* Graph)
	{
		FBox Bounds(ForceInit);
		if (ASWGShip* Ship = Graph ? Graph->GetLocalPlayerShip() : nullptr)
		{
			FVector Center, Extent; Ship->GetActorBounds(false, Center, Extent);
			if (!Extent.IsNearlyZero()) Bounds = FBox(Center-Extent, Center+Extent);
		}
		return Bounds;
	}
}

bool USWGSpaceDustComponent::Setup(const FSWGSpaceTerrainData& Data, const FString& TerrainVirtualPath, USWGTreSubsystem* TreSubsystem, USWGMeshGeneratorSubsystem* MeshGenerator)
{
	++Generation;
	Dust = nullptr; DustMaterial = nullptr; Positions.Reset(); bHasPose = false; DustRadius = Data.DustRadius; StreakWorld = FVector::ZeroVector;
	Fields.Reset(); Debris.Reset(); DebrisMeshes.Reset(); DebrisNativeSize.Reset(); FieldStyles.Reset(); ActiveStyle = FDebrisStyle(); ActiveField = INDEX_NONE; bDebrisActive = false; DebrisTime = 0;
	AActor* SkyActor = GetOwner();
	if (Data.DustCount > 0)
	{
		if (Data.DustRadius <= 0) { UE_LOG(LogTemp, Warning, TEXT("USWGSpaceDustComponent: %s has dust (%d) but no radius"), *TerrainVirtualPath, Data.DustCount); return false; }
		UStaticMesh* Quad = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGSpaceDust.M_SWGSpaceDust"));
		if (!Quad || !Parent)
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGSpaceDustComponent: M_SWGSpaceDust is missing; run swg.CreateSpaceDustAssets. Space dust is off."));
		}
		else
		{
			Dust = NewObject<UInstancedStaticMeshComponent>(SkyActor, TEXT("SWGSpaceDust"));
			Dust->SetupAttachment(SkyActor->GetRootComponent());
			Dust->SetAbsolute(true, true, true); Dust->SetMobility(EComponentMobility::Movable);
			Dust->SetStaticMesh(Quad); Dust->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Dust->SetCastShadow(false); Dust->bAffectDistanceFieldLighting = false; Dust->bDisableCollision = true;
			// The material is translucent with world-position offset, which Nanite cannot draw.
			Dust->bDisallowNanite = true;
			DustMaterial = UMaterialInstanceDynamic::Create(Parent, SkyActor);
			DustMaterial->SetScalarParameterValue(TEXT("DustRadius"), Data.DustRadius*SWGWorldScale);
			DustMaterial->SetScalarParameterValue(TEXT("StreakBoost"), StreakBoost);
			Dust->SetMaterial(0, DustMaterial);
			Dust->RegisterComponent();
			Positions.SetNum(FSWGSpaceQuality::ScaleDustCount(Data.DustCount));
			FRandomStream Random(int32(GetTypeHash(TerrainVirtualPath)));
			for (FVector& Position : Positions)
			{
				Position = Random.VRand()*FMath::Sqrt(Random.FRand())*Data.DustRadius*SWGWorldScale;
				Dust->AddInstance(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.0)), true);
			}
		}
	}

	// Debris only appears inside the asteroid fields the zone's own table authors.
	const FString FieldPath = TEXT("datatables/space/asteroidfield/")+FPaths::GetBaseFilename(TerrainVirtualPath)+TEXT(".iff");
	if (!TreSubsystem || !TreSubsystem->FileExists(FieldPath)) return true;
	FSWGDataTableData Table;
	if (!FSWGDataTableReader::ReadDataTable(TreSubsystem->CreateIffReader(FieldPath), Table) || !FSWGAsteroidFieldReader::Read(Table, Fields)) { UE_LOG(LogTemp, Warning, TEXT("USWGSpaceDustComponent: cannot read %s"), *FieldPath); return false; }
	if (Fields.IsEmpty() || !CVarSpaceDebris.GetValueOnGameThread() || !MeshGenerator) return true;
	// Each field names a radial style table; one read per distinct table. A table that fails to read leaves that field on the defaults.
	TMap<FString, TArray<FSWGAsteroidRadialStyleRow>> StyleTables;
	int32 Budget = 0;
	for (const FSWGAsteroidFieldData& Field : Fields)
	{
		TArray<FSWGAsteroidRadialStyleRow>* Rows = StyleTables.Find(Field.RadialStyleTable);
		if (!Rows)
		{
			Rows = &StyleTables.Add(Field.RadialStyleTable);
			FSWGDataTableData StyleTable;
			if (Field.RadialStyleTable.IsEmpty() || !TreSubsystem->FileExists(Field.RadialStyleTable)
				|| !FSWGDataTableReader::ReadDataTable(TreSubsystem->CreateIffReader(Field.RadialStyleTable), StyleTable) || !FSWGAsteroidFieldReader::ReadRadialStyle(StyleTable, *Rows))
			{
				UE_LOG(LogTemp, Warning, TEXT("USWGSpaceDustComponent: radial style '%s' cannot be read; its fields use the default debris style."), *Field.RadialStyleTable);
				Rows->Reset();
			}
		}
		FieldStyles.Add(MakeDebrisStyle(Field, *Rows));
		Budget = FMath::Max(Budget, FieldStyles.Last().Count);
	}
	FRandomStream Random(int32(GetTypeHash(TerrainVirtualPath))+101);
	for (int32 Index = 0; Index < Budget; ++Index)
	{
		FDebrisPiece& Piece = Debris.AddDefaulted_GetRef();
		Piece.Position = Random.VRand()*FMath::Pow(Random.FRand(), 1.0/3.0)*DefaultDebrisRange;
		Piece.Drift = Random.VRand()*Random.FRand()*DebrisMaxDrift;
		Piece.SpinAxis = Random.VRand(); Piece.SpinRate = Random.FRandRange(.1f, .6f); Piece.Phase = Random.FRandRange(0.f, UE_TWO_PI);
		Piece.SizeRoll = Random.FRand();
		Piece.MeshIndex = Index % DebrisMeshCount;
	}
	if (Debris.IsEmpty()) return true;
	DebrisMeshes.SetNum(DebrisMeshCount); DebrisNativeSize.Init(0.f, DebrisMeshCount);
	const TWeakObjectPtr<USWGSpaceDustComponent> WeakThis(this);
	for (int32 MeshIndex = 0; MeshIndex < DebrisMeshCount; ++MeshIndex)
		MeshGenerator->RequestAppearanceMesh(DebrisMeshPaths[MeshIndex], [WeakThis, MeshIndex, RequestGeneration = Generation](UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
		{
			if (USWGSpaceDustComponent* Self = WeakThis.Get(); Self && Self->Generation == RequestGeneration) Self->OnDebrisMeshReady(MeshIndex, Mesh, Materials);
		});
	return true;
}

void USWGSpaceDustComponent::OnDebrisMeshReady(int32 MeshIndex, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
{
	AActor* SkyActor = GetOwner();
	if (!Mesh || !SkyActor || !DebrisMeshes.IsValidIndex(MeshIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGSpaceDustComponent: debris mesh %d failed to load; that rock type is skipped."), MeshIndex);
		return;
	}
	const float Radius = Mesh->GetBounds().SphereRadius;
	if (Radius < UE_SMALL_NUMBER) return;
	DebrisNativeSize[MeshIndex] = 2.f*Radius;
	UInstancedStaticMeshComponent* Layer = NewObject<UInstancedStaticMeshComponent>(SkyActor, *FString::Printf(TEXT("SWGSpaceDebris%d"), MeshIndex));
	Layer->SetupAttachment(SkyActor->GetRootComponent());
	Layer->SetAbsolute(true, true, true); Layer->SetMobility(EComponentMobility::Movable);
	Layer->SetStaticMesh(Mesh); Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Layer->SetCastShadow(false); Layer->bAffectDistanceFieldLighting = false; Layer->bDisableCollision = true; Layer->bDisallowNanite = true;
	for (int32 Slot = 0; Slot < Materials.Num(); ++Slot) if (Materials[Slot]) Layer->SetMaterial(Slot, Materials[Slot]);
	Layer->RegisterComponent();
	// Instances are added in piece order, so a layer's instance indices run 0..n-1 in that order.
	for (FDebrisPiece& Piece : Debris)
		if (Piece.MeshIndex == MeshIndex) Piece.InstanceIndex = Layer->AddInstance(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.0)), true);
	Layer->SetVisibility(bDebrisActive);
	DebrisMeshes[MeshIndex] = Layer;
}

void USWGSpaceDustComponent::SetStreakBoost(float Boost)
{
	StreakBoost = FMath::Max(Boost, 0.f);
	if (IsValid(DustMaterial)) DustMaterial->SetScalarParameterValue(TEXT("StreakBoost"), StreakBoost);
}

float USWGSpaceDustComponent::ComputeStreakBoost(float SpeedMeters, float MaxSpeed, float BoostMaxSpeed)
{
	if (MaxSpeed <= 0.f || SpeedMeters <= MaxSpeed) return 1.f;
	const float Span = BoostMaxSpeed > MaxSpeed ? BoostMaxSpeed-MaxSpeed : MaxSpeed*.5f;
	const float Alpha = FMath::Clamp((SpeedMeters-MaxSpeed)/Span, 0.f, 1.f);
	return FMath::Lerp(1.f, MaxStreakBoost, Alpha*Alpha*(3.f-2.f*Alpha));
}

USWGSpaceDustComponent::FDebrisStyle USWGSpaceDustComponent::MakeDebrisStyle(const FSWGAsteroidFieldData& Field, const TArray<FSWGAsteroidRadialStyleRow>& StyleRows)
{
	FDebrisStyle Style;
	Style.Count = FMath::Clamp(Field.NumRadialAsteroids, 0, MaxDebrisCount);
	double Weight = 0, SpawnRange = 0, DeleteRange = 0, ScaleMin = 0, ScaleMax = 0;
	for (const FSWGAsteroidRadialStyleRow& Row : StyleRows)
	{
		Weight += Row.Likelihood;
		SpawnRange += Row.SpawnRange*Row.Likelihood; DeleteRange += Row.DeleteRange*Row.Likelihood;
		ScaleMin += Row.ScaleMin*Row.Likelihood; ScaleMax += Row.ScaleMax*Row.Likelihood;
	}
	if (Weight > 0)
	{
		// Table ranges are metres, like the field rows' distances.
		Style.SpawnRange = FMath::Clamp(SpawnRange/Weight*SWGWorldScale, MinDebrisRange, MaxDebrisRange);
		Style.DeleteRange = FMath::Clamp(DeleteRange/Weight*SWGWorldScale, Style.SpawnRange, MaxDebrisRange);
		Style.ScaleMin = float(ScaleMin/Weight); Style.ScaleMax = float(ScaleMax/Weight);
	}
	else
	{
		Style.Count = Field.NumRadialAsteroids > 0 ? DefaultDebrisCount : 0;
		Style.ScaleMin = .5f; Style.ScaleMax = 1.f;
	}
	Style.ScaleMin *= Field.ScaleMin; Style.ScaleMax *= Field.ScaleMax;
	return Style;
}

int32 USWGSpaceDustComponent::FindAsteroidField(const FVector& SpacePosition) const
{
	for (int32 FieldIndex = 0; FieldIndex < Fields.Num(); ++FieldIndex)
	{
		const FSWGAsteroidFieldData& Field = Fields[FieldIndex];
		const double Radius = double(Field.Radius)*SWGWorldScale;
		if (!Field.bSpline)
		{
			if (FVector::DistSquared(SpacePosition, NativeToSpace(Field.Center)) <= FMath::Square(Radius)) return FieldIndex;
			continue;
		}
		for (int32 Point = 0; Point+1 < Field.ControlPoints.Num(); ++Point)
		{
			const FVector Closest = FMath::ClosestPointOnSegment(SpacePosition, NativeToSpace(Field.ControlPoints[Point]), NativeToSpace(Field.ControlPoints[Point+1]));
			if (FVector::DistSquared(SpacePosition, Closest) <= FMath::Square(Radius)) return FieldIndex;
		}
	}
	return INDEX_NONE;
}

bool USWGSpaceDustComponent::IsInsideAsteroidField(const FVector& SpacePosition) const
{
	return FindAsteroidField(SpacePosition) != INDEX_NONE;
}

void USWGSpaceDustComponent::ScatterDebris(const FVector& Camera, int32 FieldIndex)
{
	ActiveField = FieldIndex; ActiveStyle = FieldStyles[FieldIndex];
	FRandomStream Random(FieldIndex*7919+13);
	for (FDebrisPiece& Piece : Debris)
		Piece.Position = Camera + Random.VRand()*FMath::Pow(Random.FRand(), 1.0/3.0)*ActiveStyle.SpawnRange;
	DebrisPreviousCamera = Camera;
}

void USWGSpaceDustComponent::Update(const FVector& Viewpoint, const TArray<TObjectPtr<ADirectionalLight>>& Lights, float DeltaTime)
{
	if (!IsValid(Dust) && Debris.IsEmpty()) return;
	USWGObjectGraphSubsystem* Graph = GetWorld()->GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>();
	const FTransform Frame = Graph ? Graph->GetSpaceFrame() : FTransform::Identity;
	const FVector Camera = Frame.TransformPosition(Viewpoint);
	const AActor* Owner = Graph ? Graph->FindActor(Graph->GetLocalPlayerObjectId()) : nullptr;
	const FVector OwnerPosition = Owner ? Frame.TransformPosition(Owner->GetActorLocation()) : Frame.GetLocation();
	if (!bHasPose)
	{
		for (FVector& Position : Positions) Position += Camera;
		for (FDebrisPiece& Piece : Debris) Piece.Position += Camera;
		PreviousCamera = Camera; PreviousOwner = OwnerPosition; DebrisPreviousCamera = Camera; bHasPose = true;
	}
	const FBox ShipBounds = GetShipBounds(Graph);
	if (IsValid(Dust))
	{
		const FVector Displacement = (OwnerPosition-PreviousOwner).GetClampedToMaxSize(SWGWorldScale);
		StreakWorld = Frame.InverseTransformVector(-Displacement);
		if (const ASWGShip* Ship = Graph ? Graph->GetLocalPlayerShip() : nullptr)
		{
			const FShipObjectBaseline& State = Ship->ShipState;
			if (State.Flight.MaxSpeed > 0.f) SetStreakBoost(ComputeStreakBoost(Ship->SpaceVelocity.Size()/SWGWorldScale, State.Flight.MaxSpeed, State.bHasBase1 ? State.OwnerValues[BoosterMaxSpeedIndex] : 0.f));
		}
		const double RadiusSquared = FMath::Square(double(DustRadius)*SWGWorldScale);
		TArray<FTransform> Transforms; Transforms.Reserve(Positions.Num());
		for (FVector& Position : Positions)
		{
			if (FVector::DistSquared(Position, Camera) > RadiusSquared) Position = Camera-(Position-PreviousCamera);
			const FVector Start = Frame.InverseTransformPosition(Position), End = Frame.InverseTransformPosition(Position-Displacement);
			// shortcut: hull bounds keep dust out of POB rooms; replace with portal clipping when precise exterior visibility is available.
			const bool bInsideHull = ShipBounds.IsValid && (ShipBounds.IsInside(Start) || ShipBounds.IsInside(End));
			Transforms.Add(FTransform(FQuat::Identity, Start, bInsideHull ? FVector(0.0) : FVector(1.0)));
		}
		Dust->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
		if (IsValid(DustMaterial))
		{
			DustMaterial->SetVectorParameterValue(TEXT("StreakVector"), FLinearColor(StreakWorld.X, StreakWorld.Y, StreakWorld.Z, 0));
			// Lights face along their travel; the material glints when the camera looks toward the key light.
			const FVector LightTravel = Lights.IsEmpty() || !IsValid(Lights[0]) ? FVector(0, 0, -1) : Lights[0]->GetActorForwardVector();
			DustMaterial->SetVectorParameterValue(TEXT("LightDirection"), FLinearColor(LightTravel.X, LightTravel.Y, LightTravel.Z, 0));
		}
	}
	UpdateDebris(Frame, Camera, ShipBounds, DeltaTime);
	PreviousCamera = Camera; PreviousOwner = OwnerPosition;
}

void USWGSpaceDustComponent::UpdateDebris(const FTransform& Frame, const FVector& Camera, const FBox& ShipBounds, float DeltaTime)
{
	if (Debris.IsEmpty()) return;
	DebrisTime += DeltaTime;
	// Entering another field restyles the rocks and gathers them around the camera.
	const int32 FieldIndex = FindAsteroidField(Camera);
	if (FieldIndex != INDEX_NONE && FieldIndex != ActiveField && FieldStyles.IsValidIndex(FieldIndex)) ScatterDebris(Camera, FieldIndex);
	// The rocks are in absolute space like the dust: reflecting about the camera keeps them streaming past as the ship moves.
	const double DeleteRange = ActiveField != INDEX_NONE ? ActiveStyle.DeleteRange : DefaultDebrisRange;
	for (FDebrisPiece& Piece : Debris)
	{
		Piece.Position += Piece.Drift*DeltaTime;
		if (FVector::DistSquared(Piece.Position, Camera) > FMath::Square(DeleteRange)) Piece.Position = Camera-(Piece.Position-DebrisPreviousCamera);
	}
	DebrisPreviousCamera = Camera;
	bDebrisActive = CVarSpaceDebris.GetValueOnGameThread() && FieldIndex != INDEX_NONE && ActiveStyle.Count > 0;
	const FBox HullBounds = ShipBounds.IsValid ? ShipBounds.ExpandBy(DebrisHullMargin) : ShipBounds;
	TArray<TArray<FTransform>> Transforms; Transforms.SetNum(DebrisMeshCount);
	for (int32 PieceIndex = 0; PieceIndex < Debris.Num(); ++PieceIndex)
	{
		const FDebrisPiece& Piece = Debris[PieceIndex];
		if (Piece.InstanceIndex == INDEX_NONE) continue;
		const FVector WorldPosition = Frame.InverseTransformPosition(Piece.Position);
		const bool bInsideHull = HullBounds.IsValid && HullBounds.IsInside(WorldPosition);
		// Rocks past the active style's count stay in the layer at zero scale, so instance indices never change.
		const float Diameter = FMath::Clamp(DebrisNativeSize[Piece.MeshIndex]*FMath::Lerp(ActiveStyle.ScaleMin, ActiveStyle.ScaleMax, Piece.SizeRoll), MinDebrisSize, MaxDebrisSize);
		const double Scale = bInsideHull || PieceIndex >= ActiveStyle.Count ? 0.0 : double(Diameter)/DebrisNativeSize[Piece.MeshIndex];
		const FQuat Spin = FQuat(Piece.SpinAxis, Piece.Phase+Piece.SpinRate*DebrisTime);
		Transforms[Piece.MeshIndex].Add(FTransform(Frame.GetRotation().Inverse()*Spin, WorldPosition, FVector(Scale)));
	}
	for (int32 MeshIndex = 0; MeshIndex < DebrisMeshCount; ++MeshIndex)
	{
		UInstancedStaticMeshComponent* Layer = DebrisMeshes.IsValidIndex(MeshIndex) ? DebrisMeshes[MeshIndex].Get() : nullptr;
		if (!Layer) continue;
		Layer->SetVisibility(bDebrisActive);
		if (bDebrisActive && !Transforms[MeshIndex].IsEmpty()) Layer->BatchUpdateInstancesTransforms(0, Transforms[MeshIndex], true, true, true);
	}
}
