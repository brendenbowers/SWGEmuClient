#include "SWGHoloRadarActor.h"
#include "SWGHoloShipActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr const TCHAR* RadarMaterialPath = TEXT("/Game/SWGEmu/Materials/M_SWGHoloRadar.M_SWGHoloRadar");
	constexpr double ShipScanSeconds = 0.5;
	/** Fractions of the nominal radius. */
	constexpr float MarkerRadius = 0.045f;
	constexpr float TargetMarkerRadius = 0.07f;
	constexpr float StalkWidth = 0.012f;
	constexpr float MinimumStalkLength = 0.5f;
	constexpr float MiniatureRadius = 0.2f;

	FLinearColor Srgb(uint8 Red, uint8 Green, uint8 Blue) { return FLinearColor::FromSRGBColor(FColor(Red, Green, Blue)); }

	/** Glow colours: the retail text cyan for neutral, the value green for friendly, the low-condition red for hostile, the lead amber for the target. */
	FLinearColor ClassColour(ESWGRadarBlipClass BlipClass)
	{
		switch (BlipClass)
		{
		case ESWGRadarBlipClass::Friendly: return Srgb(0x62, 0xFF, 0x15);
		case ESWGRadarBlipClass::Hostile: return Srgb(0xFF, 0x2D, 0x2D);
		case ESWGRadarBlipClass::Structure: return Srgb(0xB8, 0xD4, 0xFF);
		case ESWGRadarBlipClass::Target: return Srgb(0xFF, 0xAC, 0x00);
		default: return Srgb(0x96, 0xF4, 0xFC);
		}
	}

	ESWGRadarBlipClass ClassifyBlip(const FSWGSpaceHudBlip& Blip)
	{
		if (Blip.bTarget) return ESWGRadarBlipClass::Target;
		if (Blip.Kind == ESWGSpaceHudBlipKind::Structure) return ESWGRadarBlipClass::Structure;
		switch (Blip.Relation)
		{
		case ESWGSpaceHudRelation::Friendly: return ESWGRadarBlipClass::Friendly;
		case ESWGSpaceHudRelation::Hostile: return ESWGRadarBlipClass::Hostile;
		default: return ESWGRadarBlipClass::Neutral;
		}
	}
}

ASWGHoloRadarActor::ASWGHoloRadarActor()
{
	DiscDiameter = NominalRadius * 2.2f;
	DroidScale = 1.4f;
	DroidReach = 0.9f;
	ProjectionLift = 3.f;
	HoloIntensity = 0.55f;
	RayWidth = 0.6f;

	SphereRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SphereRoot"));
	SphereRoot->SetupAttachment(GetRootComponent());
	SphereRoot->SetRelativeLocation(FVector(0.f, 0.f, CentreLift));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	auto MakeMesh = [this](const TCHAR* Name, UStaticMesh* Mesh, int32 SortPriority)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(SphereRoot);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->bDisallowNanite = true;
		Component->SetTranslucentSortPriority(SortPriority);
		return Component;
	};
	const float ShellScale = NominalRadius / 50.f;
	Shell = MakeMesh(TEXT("Shell"), SphereFinder.Object, 1);
	Shell->SetRelativeScale3D(FVector(ShellScale));
	Plane = MakeMesh(TEXT("RangePlane"), PlaneFinder.Object, 0);
	Plane->SetRelativeScale3D(FVector(ShellScale));
	HeadingMarker = MakeMesh(TEXT("HeadingMarker"), ConeFinder.Object, 2);
	HeadingMarker->SetRelativeLocation(FVector(NominalRadius * 1.08f, 0.f, 0.f));
	HeadingMarker->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	HeadingMarker->SetRelativeScale3D(FVector(0.07f, 0.07f, 0.1f) * ShellScale);
	NorthMarker = MakeMesh(TEXT("NorthMarker"), ConeFinder.Object, 2);
	NorthMarker->SetRelativeScale3D(FVector(0.05f, 0.05f, 0.08f) * ShellScale);
	NorthMarker->SetVisibility(false);

	MiniatureRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MiniatureRoot"));
	MiniatureRoot->SetupAttachment(SphereRoot);

	// One instanced mesh per blip class; cubes mark structures, spheres everything else.
	for (int32 ClassIndex = 0; ClassIndex < int32(ESWGRadarBlipClass::Count); ++ClassIndex)
	{
		UInstancedStaticMeshComponent* BlipComponent = CreateDefaultSubobject<UInstancedStaticMeshComponent>(*FString::Printf(TEXT("Blips%d"), ClassIndex));
		BlipComponent->SetupAttachment(SphereRoot);
		BlipComponent->SetStaticMesh(ESWGRadarBlipClass(ClassIndex) == ESWGRadarBlipClass::Structure ? CubeFinder.Object : SphereFinder.Object);
		BlipComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BlipComponent->SetCastShadow(false);
		BlipComponent->bDisallowNanite = true;
		BlipComponent->SetTranslucentSortPriority(3);
		BlipMeshes.Add(BlipComponent);
	}
	StalkMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Stalks"));
	StalkMesh->SetupAttachment(SphereRoot);
	StalkMesh->SetStaticMesh(CylinderFinder.Object);
	StalkMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StalkMesh->SetCastShadow(false);
	StalkMesh->bDisallowNanite = true;
	StalkMesh->SetTranslucentSortPriority(1);
}

UMaterialInstanceDynamic* ASWGHoloRadarActor::MakeRadarMaterial(float Mode, const FLinearColor& Colour, float Intensity)
{
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(RadarParent, this);
	Material->SetScalarParameterValue(TEXT("Mode"), Mode);
	Material->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	Material->SetVectorParameterValue(TEXT("Color"), Colour);
	Material->SetScalarParameterValue(TEXT("Seed"), FMath::Frac(RadarMaterials.Num() * 0.6180339f));
	Material->SetScalarParameterValue(TEXT("Reveal"), FadeAlpha);
	RadarMaterials.Add(Material);
	return Material;
}

void ASWGHoloRadarActor::EnsureMaterials()
{
	if (bMaterialsReady) return;
	RadarParent = LoadObject<UMaterialInterface>(nullptr, RadarMaterialPath);
	if (!RadarParent)
	{
		UE_LOG(LogTemp, Warning, TEXT("ASWGHoloRadarActor: M_SWGHoloRadar is missing; run swg.CreateHoloUIAssets. The radar will not draw."));
		return;
	}
	bMaterialsReady = true;
	const FLinearColor Cyan = ClassColour(ESWGRadarBlipClass::Neutral);
	Shell->SetMaterial(0, MakeRadarMaterial(0.f, Cyan, 0.9f));
	Plane->SetMaterial(0, MakeRadarMaterial(1.f, Cyan, 1.1f));
	HeadingMarker->SetMaterial(0, MakeRadarMaterial(2.f, ClassColour(ESWGRadarBlipClass::Structure), 1.4f));
	NorthMarker->SetMaterial(0, MakeRadarMaterial(2.f, ClassColour(ESWGRadarBlipClass::Target), 1.4f));
	for (int32 ClassIndex = 0; ClassIndex < int32(ESWGRadarBlipClass::Count); ++ClassIndex)
	{
		BlipMeshes[ClassIndex]->SetMaterial(0, MakeRadarMaterial(2.f, ClassColour(ESWGRadarBlipClass(ClassIndex)), 1.8f));
	}
	StalkMesh->SetMaterial(0, MakeRadarMaterial(2.f, Cyan, 0.55f));
	MiniatureMaterial = MakeHoloMaterial(HoloColor, HoloIntensity);
}

void ASWGHoloRadarActor::BeginPlay()
{
	Super::BeginPlay();
	EnsureMaterials();
}

void ASWGHoloRadarActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	for (UMaterialInstanceDynamic* Material : RadarMaterials)
	{
		if (Material) Material->SetScalarParameterValue(TEXT("Reveal"), FadeAlpha);
	}
	const double Now = FPlatformTime::Seconds();
	if (Now < NextShipScanTime) return;
	NextShipScanTime = Now + ShipScanSeconds;
	if (SWGHoloShipCopy::Signature(Ship.Get()) != MiniatureSignature) RebuildMiniature();
}

void ASWGHoloRadarActor::SetShip(const AActor* InShip)
{
	Ship = InShip;
	EnsureMaterials();
	RebuildMiniature();
}

void ASWGHoloRadarActor::RebuildMiniature()
{
	SWGHoloShipCopy::Clear(MiniatureCopies);
	MiniatureSignature = SWGHoloShipCopy::Signature(Ship.Get());
	const AActor* Source = Ship.Get();
	if (!Source || !MiniatureMaterial) return;
	SWGHoloShipCopy::Build(*this, *MiniatureRoot, *Source, MiniatureMaterial, NominalRadius * MiniatureRadius, MiniatureCopies);
	for (UStaticMeshComponent* Copy : MiniatureCopies) Copy->SetTranslucentSortPriority(3);
}

void ASWGHoloRadarActor::SetContents(const TArray<FSWGSpaceHudBlip>& InBlips, float InRange, const TOptional<FVector>& InNorth)
{
	EnsureMaterials();
	Blips = InBlips;
	Range = InRange;
	North = InNorth;
	RefreshBlips();
}

void ASWGHoloRadarActor::RefreshBlips()
{
	TArray<TArray<FTransform>> Markers;
	Markers.SetNum(int32(ESWGRadarBlipClass::Count));
	TArray<FTransform> Stalks;
	BlipPositions.Reset();
	BlipClasses.Reset();
	const float Pulse = 1.f + 0.25f * FMath::Sin(DroidTime * 6.f);
	for (const FSWGSpaceHudBlip& Blip : Blips)
	{
		const FVector Position = SWGSpaceHud::RadarSpherePosition(Blip.Relative, Range) * NominalRadius;
		const ESWGRadarBlipClass BlipClass = ClassifyBlip(Blip);
		BlipPositions.Add(Position);
		BlipClasses.Add(BlipClass);
		const float Radius = (BlipClass == ESWGRadarBlipClass::Target ? TargetMarkerRadius * Pulse : MarkerRadius) * NominalRadius;
		// The unit meshes are 100 across, so a marker of radius r scales by r / 50.
		Markers[int32(BlipClass)].Add(FTransform(FRotator(0.f, 45.f, 0.f), Position, FVector(Radius / 50.f)));
		const float Height = FMath::Abs(Position.Z);
		if (Height >= MinimumStalkLength)
		{
			const float Width = StalkWidth * NominalRadius / 50.f;
			Stalks.Add(FTransform(FRotator::ZeroRotator, FVector(Position.X, Position.Y, Position.Z * 0.5f), FVector(Width, Width, Height / 100.f)));
		}
	}
	for (int32 ClassIndex = 0; ClassIndex < int32(ESWGRadarBlipClass::Count); ++ClassIndex)
	{
		BlipMeshes[ClassIndex]->ClearInstances();
		if (!Markers[ClassIndex].IsEmpty()) BlipMeshes[ClassIndex]->AddInstances(Markers[ClassIndex], false, false);
	}
	StalkMesh->ClearInstances();
	if (!Stalks.IsEmpty()) StalkMesh->AddInstances(Stalks, false, false);

	// The zone's north on the rim, pointing outward, when the ship's pose is known.
	const FVector FlatNorth = North.IsSet() ? FVector(North->X, North->Y, 0.f).GetSafeNormal() : FVector::ZeroVector;
	NorthMarker->SetVisibility(!FlatNorth.IsNearlyZero());
	if (!FlatNorth.IsNearlyZero())
	{
		NorthMarker->SetRelativeLocation(FlatNorth * NominalRadius * 1.08f);
		NorthMarker->SetRelativeRotation(FRotationMatrix::MakeFromZ(FlatNorth).Rotator());
	}
}

int32 ASWGHoloRadarActor::GetBlipCount(ESWGRadarBlipClass BlipClass) const
{
	return BlipMeshes.IsValidIndex(int32(BlipClass)) ? BlipMeshes[int32(BlipClass)]->GetInstanceCount() : 0;
}

int32 ASWGHoloRadarActor::GetStalkCount() const
{
	return StalkMesh->GetInstanceCount();
}

bool ASWGHoloRadarActor::IsNorthMarkerShown() const
{
	return NorthMarker->IsVisible();
}

FVector ASWGHoloRadarActor::GetCentreLocation() const
{
	return SphereRoot->GetComponentLocation();
}
