#include "SWGHoloProjectorActor.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "TRE/SWGCrc32.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float FadeInSeconds = 0.5f;
	/** The lens sits this far under the droid's origin; rays start there. */
	constexpr float LensDrop = 8.f;
	/** How long the sweep rays linger after the last change, and how long they take to fade. */
	constexpr double SweepLingerSeconds = 0.65;
	constexpr float SweepFadeSeconds = 0.4f;
}

ASWGHoloProjectorActor::ASWGHoloProjectorActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Projector")));

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
	DroidLens->SetRelativeLocation(FVector(0.f, 0.f, -LensDrop));
	DroidLens->SetRelativeScale3D(FVector(0.04f));

	DroidLensLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("DroidLensLight"));
	DroidLensLight->SetupAttachment(DroidLens);
	DroidLensLight->SetCastShadows(false);
	DroidLensLight->SetIntensityUnits(ELightUnits::Candelas);
	DroidLensLight->SetIntensity(4.f);
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	BaseComponent->SetStaticMesh(BeamMesh);
	DroidLens->SetStaticMesh(SphereFinder.Object);
}

UMaterialInstanceDynamic* ASWGHoloProjectorActor::MakeHoloMaterial(const FLinearColor& Color, float Intensity)
{
	UMaterialInstanceDynamic* Material = HoloMaterial ? UMaterialInstanceDynamic::Create(HoloMaterial, this) : nullptr;
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		Material->SetScalarParameterValue(TEXT("Radius"), GetFadeRadius());
		Material->SetScalarParameterValue(TEXT("Fade"), FadeAlpha);
		HoloMaterials.Add(Material);
	}
	return Material;
}

void ASWGHoloProjectorActor::BeginPlay()
{
	Super::BeginPlay();
	if (!bHasProjector)
	{
		BaseComponent->SetVisibility(false);
		DroidRoot->SetVisibility(false, /*bPropagateToChildren=*/true);
		GlowLight->SetVisibility(false);
		return;
	}
	// The projector: a thin glowing disc a touch wider than the image.
	const float BaseScale = DiscDiameter * 1.05f / 100.f;
	BaseComponent->SetRelativeScale3D(FVector(BaseScale, BaseScale, 0.03f));
	// A faint light field under the image; the droid above is the projector.
	BaseComponent->SetMaterial(0, MakeHoloMaterial(HoloColor, HoloIntensity * 0.12f));
	DroidLens->SetMaterial(0, MakeHoloMaterial(HoloColor, HoloIntensity * 2.f));
	DroidLensLight->SetLightColor(HoloColor);
	auto MakeLine = [this](float Intensity)
	{
		UStaticMeshComponent* Line = NewObject<UStaticMeshComponent>(this);
		Line->SetStaticMesh(BeamMesh);
		Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Line->SetCastShadow(false);
		Line->SetMaterial(0, MakeHoloMaterial(HoloColor, Intensity));
		Line->SetupAttachment(GetRootComponent());
		Line->RegisterComponent();
		return Line;
	};
	for (int32 Index = 0; Index < ProjectionRayCount; ++Index)
	{
		ProjectionLines.Add(MakeLine(HoloIntensity * 0.12f));
	}
	for (int32 Index = 0; Index < SweepRayCount; ++Index)
	{
		SweepLines.Add(MakeLine(0.f));
	}
	RequestDroid();
	UpdateDroid();
}

void ASWGHoloProjectorActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FadeAlpha = FMath::Min(1.f, FadeAlpha + DeltaSeconds / FadeInSeconds);
	DroidTime += DeltaSeconds;
	if (bHasProjector)
	{
		UpdateDroid();
	}
	// The material fades anything too far from here.
	const FVector Centre = GetActorLocation();
	HoloMaterials.RemoveAll([](const TObjectPtr<UMaterialInstanceDynamic>& Material) { return !Material; });
	for (UMaterialInstanceDynamic* Material : HoloMaterials)
	{
		Material->SetVectorParameterValue(TEXT("Center"), FLinearColor(Centre.X, Centre.Y, Centre.Z, 0.f));
		Material->SetScalarParameterValue(TEXT("Fade"), FadeAlpha);
	}
}

void ASWGHoloProjectorActor::SetDroidSide(const FVector2D& Direction)
{
	DroidSide = Direction.GetSafeNormal();
	if (DroidSide.IsNearlyZero())
	{
		DroidSide = FVector2D(1.f, 0.f);
	}
	UpdateDroid();
}

void ASWGHoloProjectorActor::SetExtraRays(const TArray<FVector>& WorldTargets, const TArray<float>& Brightness)
{
	ExtraTargets = WorldTargets;
	ExtraBrightness = Brightness;
	while (bHasProjector && ExtraLines.Num() < ExtraTargets.Num())
	{
		UStaticMeshComponent* Line = NewObject<UStaticMeshComponent>(this);
		Line->SetStaticMesh(BeamMesh);
		Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Line->SetCastShadow(false);
		Line->SetMaterial(0, MakeHoloMaterial(HoloColor, 0.f));
		Line->SetupAttachment(GetRootComponent());
		Line->RegisterComponent();
		ExtraLines.Add(Line);
	}
}

FVector ASWGHoloProjectorActor::GetDroidLocation() const
{
	return DroidRoot->GetComponentLocation();
}

void ASWGHoloProjectorActor::RequestDroid()
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	if (!MeshGenerator || DroidTemplate.IsEmpty())
	{
		return;
	}
	TWeakObjectPtr<ASWGHoloProjectorActor> WeakThis(this);
	auto Attach = [WeakThis](UMeshComponent* Mesh, const TArray<UMaterialInterface*>& Materials)
	{
		ASWGHoloProjectorActor* Actor = WeakThis.Get();
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
			ASWGHoloProjectorActor* Actor = WeakThis.Get();
			if (Actor && Mesh)
			{
				UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Actor);
				Component->SetStaticMesh(Mesh);
				Attach(Component, Materials);
			}
		},
		[WeakThis, Attach](USkeletalMesh* Mesh, const FSWGMeshData, const TArray<UMaterialInterface*>& Materials)
		{
			ASWGHoloProjectorActor* Actor = WeakThis.Get();
			if (Actor && Mesh)
			{
				USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Actor);
				Component->SetSkeletalMeshAsset(Mesh);
				Attach(Component, Materials);
			}
		});
}

FVector ASWGHoloProjectorActor::GetDroidHover() const
{
	const float DiscRadius = DiscDiameter * 0.5f;
	return FVector(DroidSide.X, DroidSide.Y, 0.f) * DiscRadius * DroidReach
		+ FVector(0.f, 0.f, ProjectionLift + DiscRadius * DroidHeight);
}

FVector ASWGHoloProjectorActor::RimPoint(float AngleDegrees) const
{
	const FVector2D RimDirection = DroidSide.GetRotated(AngleDegrees + GetRimAngleOffset());
	const float RimRadius = DiscDiameter * 0.5f * 0.96f;
	return FVector(RimDirection.X * RimRadius, RimDirection.Y * RimRadius, ProjectionLift);
}

FVector ASWGHoloProjectorActor::GetProjectionTarget(int32 RayIndex) const
{
	// Span the far half of the rim, leaving the near side out of the player's view.
	return RimPoint(-90.f + 180.f * RayIndex / FMath::Max(1, ProjectionRayCount - 1));
}

FVector ASWGHoloProjectorActor::GetSweepTarget(int32 RayIndex, float Phase) const
{
	// Alternate rays sweep the far half in opposite directions.
	return RimPoint(RayIndex % 2 == 0 ? -90.f + 180.f * Phase : 90.f - 180.f * Phase);
}

void ASWGHoloProjectorActor::PlaceRay(UStaticMeshComponent* Line, const FVector& Origin, const FVector& Target) const
{
	// Engine cylinders are 100 units tall and across.
	const FVector Ray = Target - Origin;
	Line->SetRelativeLocation((Origin + Target) * 0.5f);
	Line->SetRelativeRotation(FRotationMatrix::MakeFromZ(Ray).Rotator());
	Line->SetRelativeScale3D(FVector(RayWidth / 100.f, RayWidth / 100.f, Ray.Size() / 100.f));
}

void ASWGHoloProjectorActor::UpdateDroid()
{
	// A slow bob and a slight sway, so it reads as hovering rather than fixed.
	const float Bob = FMath::Sin(DroidTime * 1.6f) * 3.f;
	const FVector Hover = GetDroidHover() + FVector(0.f, 0.f, Bob);
	const FVector ToCentre = FVector(0.f, 0.f, ProjectionLift) - Hover;
	DroidRoot->SetRelativeLocation(Hover);
	DroidRoot->SetRelativeRotation(FRotator(0.f, ToCentre.Rotation().Yaw + GetDroidExtraYaw() + FMath::Sin(DroidTime * 0.7f) * 8.f, 0.f));

	const FVector Origin = Hover + FVector(0.f, 0.f, -LensDrop);
	for (int32 Index = 0; Index < ProjectionLines.Num(); ++Index)
	{
		PlaceRay(ProjectionLines[Index], Origin, GetProjectionTarget(Index));
	}
	const float IdleSeconds = FPlatformTime::Seconds() - LastProjectionChangeTime;
	const float SweepFade = FMath::Clamp((SweepLingerSeconds - IdleSeconds) / SweepFadeSeconds, 0.f, 1.f);
	const float Phase = FMath::Frac((DroidTime - SweepStartTime) * 1.2f);
	for (int32 Index = 0; Index < SweepLines.Num(); ++Index)
	{
		UStaticMeshComponent* Line = SweepLines[Index];
		PlaceRay(Line, Origin, GetSweepTarget(Index, Phase));
		Line->SetVisibility(SweepFade > 0.01f);
		if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Line->GetMaterial(0)))
		{
			Material->SetScalarParameterValue(TEXT("Intensity"), HoloIntensity * 0.16f * SweepFade);
		}
	}
	const FTransform& ActorToWorld = GetActorTransform();
	for (int32 Index = 0; Index < ExtraLines.Num(); ++Index)
	{
		UStaticMeshComponent* Line = ExtraLines[Index];
		const bool bUsed = ExtraTargets.IsValidIndex(Index);
		Line->SetVisibility(bUsed);
		if (!bUsed)
		{
			continue;
		}
		PlaceRay(Line, Origin, ActorToWorld.InverseTransformPosition(ExtraTargets[Index]));
		if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Line->GetMaterial(0)))
		{
			Material->SetScalarParameterValue(TEXT("Intensity"), HoloIntensity * (ExtraBrightness.IsValidIndex(Index) ? ExtraBrightness[Index] : 0.12f));
		}
	}
}

void ASWGHoloProjectorActor::NoteProjectionChange()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastProjectionChangeTime > SweepLingerSeconds)
	{
		SweepStartTime = DroidTime;
	}
	LastProjectionChangeTime = Now;
}
