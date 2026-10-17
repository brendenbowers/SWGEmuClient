#include "SWGHoloShipActor.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/** Seconds between looks at the source for hull meshes that arrived or went. */
	constexpr double SourceScanSeconds = 0.5;

	bool ShouldCopy(const UStaticMeshComponent& Component)
	{
		return Component.GetStaticMesh() && Component.IsVisible() && !Component.bHiddenInGame;
	}
}

namespace SWGHoloShipCopy
{
	FSignature Signature(const AActor* Source)
	{
		FSignature Result;
		if (!Source) return Result;
		TArray<UStaticMeshComponent*> Meshes;
		Source->GetComponents(Meshes);
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			if (ShouldCopy(*Mesh)) Result.Emplace(Mesh, Mesh->GetStaticMesh());
		}
		return Result;
	}

	float Build(AActor& Owner, USceneComponent& Parent, const AActor& Source, UMaterialInterface* Material, float FitRadius, TArray<TObjectPtr<UStaticMeshComponent>>& OutCopies)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Source.GetComponents(Meshes);
		const FTransform ActorToWorld = Source.GetActorTransform();
		TArray<TPair<const UStaticMeshComponent*, FTransform>> Parts;
		FBox Bounds(ForceInit);
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			if (!ShouldCopy(*Mesh)) continue;
			const FTransform InActor = Mesh->GetComponentTransform().GetRelativeTransform(ActorToWorld);
			Parts.Emplace(Mesh, InActor);
			const FBoxSphereBounds MeshBounds = Mesh->GetStaticMesh()->GetBounds().TransformBy(InActor);
			Bounds += MeshBounds.GetBox();
		}
		if (Parts.IsEmpty() || !Bounds.IsValid) return 0.f;
		const FVector Centre = Bounds.GetCenter();
		const float SourceRadius = FMath::Max(Bounds.GetExtent().Size(), 1.f);
		const float Scale = FitRadius / SourceRadius;
		for (const TPair<const UStaticMeshComponent*, FTransform>& Part : Parts)
		{
			UStaticMeshComponent* Copy = NewObject<UStaticMeshComponent>(&Owner);
			Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Copy->SetCastShadow(false);
			Copy->bDisallowNanite = true;
			Copy->SetStaticMesh(Part.Key->GetStaticMesh());
			for (int32 MaterialIndex = 0; MaterialIndex < Copy->GetNumMaterials(); ++MaterialIndex) Copy->SetMaterial(MaterialIndex, Material);
			Copy->SetupAttachment(&Parent);
			FTransform Relative = Part.Value;
			Relative.SetLocation((Relative.GetLocation() - Centre) * Scale);
			Relative.SetScale3D(Relative.GetScale3D() * Scale);
			Copy->SetRelativeTransform(Relative);
			Copy->RegisterComponent();
			OutCopies.Add(Copy);
		}
		return Scale;
	}

	void Clear(TArray<TObjectPtr<UStaticMeshComponent>>& Copies)
	{
		for (UStaticMeshComponent* Copy : Copies)
		{
			if (Copy) Copy->DestroyComponent();
		}
		Copies.Reset();
	}
}

ASWGHoloShipActor::ASWGHoloShipActor()
{
	HullRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HullRoot"));
	HullRoot->SetupAttachment(GetRootComponent());
	DiscDiameter = NominalRadius * 2.6f;
	DroidScale = 1.2f;
	ProjectionLift = 5.f;
	HoloIntensity = 0.55f;
	RayWidth = 0.6f;
	HullRoot->SetRelativeLocation(FVector(0.f, 0.f, ProjectionLift + NominalRadius * 1.1f));
	// Drawn over the cockpit like the radar; the asset is optional so an old content set still shows the (depth-tested) hologram.
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> HudMaterialFinder(TEXT("/Game/SWGEmu/Materials/M_SWGHologramHud.M_SWGHologramHud"));
	UseHoloMaterial(HudMaterialFinder.Get());
}

void ASWGHoloShipActor::SetSource(const AActor* InSource)
{
	Source = InSource;
	Rebuild();
}

void ASWGHoloShipActor::Rebuild()
{
	SWGHoloShipCopy::Clear(Copies);
	BuiltSignature = SWGHoloShipCopy::Signature(Source.Get());
	NextScanTime = FPlatformTime::Seconds() + SourceScanSeconds;
	const AActor* Ship = Source.Get();
	if (!Ship) return;
	if (!HullMaterial) HullMaterial = MakeHoloMaterial(HoloColor, HoloIntensity);
	if (SWGHoloShipCopy::Build(*this, *HullRoot, *Ship, HullMaterial, NominalRadius, Copies) > 0.f) NoteProjectionChange();
}

void ASWGHoloShipActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SpinYaw = FRotator::NormalizeAxis(SpinYaw + SpinRate * DeltaSeconds);
	HullRoot->SetRelativeRotation(FRotator(0.f, SpinYaw, 0.f));
	const double Now = FPlatformTime::Seconds();
	if (Now < NextScanTime) return;
	NextScanTime = Now + SourceScanSeconds;
	if (SWGHoloShipCopy::Signature(Source.Get()) != BuiltSignature) Rebuild();
}
