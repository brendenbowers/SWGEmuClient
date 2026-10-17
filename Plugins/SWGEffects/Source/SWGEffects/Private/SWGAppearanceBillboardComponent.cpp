#include "SWGAppearanceBillboardComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Common/SWGWorldScale.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SWGEffectsSubsystem.h"
#include "SWGEffectsTypes.h"
#include "SWGParticleEffectComponent.h"
#include "TRE/SWGBeamReader.h"
#include "TRE/SWGSpriteReader.h"

USWGAppearanceBillboardComponent::USWGAppearanceBillboardComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

float USWGAppearanceBillboardComponent::ComputeSpriteSideMeters(float BoundsRadius, float Scale)
{
	return UE_SQRT_2 * BoundsRadius * Scale;
}

FVector USWGAppearanceBillboardComponent::ComputeBeamNormal(const FVector& AxisDirection, const FVector& ViewerOffset)
{
	const FVector Axis = AxisDirection.GetSafeNormal();
	const FVector Perpendicular = ViewerOffset - Axis * FVector::DotProduct(ViewerOffset, Axis);
	if (Perpendicular.SizeSquared() > UE_KINDA_SMALL_NUMBER)
	{
		return Perpendicular.GetSafeNormal();
	}
	// Looking straight down the axis: any perpendicular will do.
	return FMath::Abs(Axis.Z) < 0.9f ? FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal() : FVector::RightVector;
}

bool USWGAppearanceBillboardComponent::SetupSprite(const FSWGSprite& Sprite, float Scale)
{
	UWorld* World = GetWorld();
	USWGEffectsSubsystem* Effects = World ? World->GetSubsystem<USWGEffectsSubsystem>() : nullptr;
	if (!Effects)
	{
		return false;
	}
	UMaterialInterface* Material = Effects->GetParticleMaterial(Sprite.GetShaderPath(), 1);
	UStaticMesh* QuadMesh = Effects->GetBillboardQuadMesh();
	if (!Material || !QuadMesh)
	{
		return false;
	}

	bBeam = false;
	const float SideCm = ComputeSpriteSideMeters(Sprite.BoundsRadius, Scale) * SWGWorldScale;

	UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
	Instances->SetStaticMesh(QuadMesh);
	Instances->SetMaterial(0, Material);
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->SetNumCustomDataFloats(USWGParticleEffectComponent::CustomDataFloats);
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(false);
	Instances->SetCanEverAffectNavigation(false);
	Instances->SetGenerateOverlapEvents(false);
	Instances->bAffectDynamicIndirectLighting = false;
	Instances->bAffectDistanceFieldLighting = false;
	// The quad billboards in WPO, so its bounds are the instance scale: a full side, doubled for slack.
	const float InstanceScale = FMath::Max(2.f * SideCm / 100.f, 0.05f);
	Instances->AddInstance(FTransform(FQuat::Identity, SWGFx::SimToUnreal(FVector(Sprite.BoundsCenter)), FVector(InstanceScale)), false);

	float CustomData[USWGParticleEffectComponent::CustomDataFloats] = {};
	CustomData[USWGParticleEffectComponent::ColorR] = Sprite.Tint.R;
	CustomData[USWGParticleEffectComponent::ColorG] = Sprite.Tint.G;
	CustomData[USWGParticleEffectComponent::ColorB] = Sprite.Tint.B;
	CustomData[USWGParticleEffectComponent::ColorA] = Sprite.Tint.A;
	CustomData[USWGParticleEffectComponent::WidthCm] = SideCm;
	CustomData[USWGParticleEffectComponent::LengthCm] = SideCm;
	Instances->SetCustomData(0, MakeArrayView(CustomData, USWGParticleEffectComponent::CustomDataFloats), true);

	Instances->SetupAttachment(this);
	Instances->RegisterComponent();
	Render = Instances;
	SetComponentTickEnabled(false);
	return true;
}

bool USWGAppearanceBillboardComponent::SetupBeam(const FSWGBeam& Beam, float Scale)
{
	UWorld* World = GetWorld();
	USWGEffectsSubsystem* Effects = World ? World->GetSubsystem<USWGEffectsSubsystem>() : nullptr;
	if (!Effects)
	{
		return false;
	}
	TArray<UMaterialInterface*> Materials;
	float SecondsPerFrame = 0.f;
	Effects->GetRibbonMaterials(Beam.ShaderPath, Materials, SecondsPerFrame);
	UStaticMesh* BeamMesh = Effects->GetBeamMesh();
	if (Materials.IsEmpty() || !BeamMesh)
	{
		return false;
	}

	bBeam = true;
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Materials[0], this);
	Material->SetVectorParameterValue(TEXT("Tint"), Beam.Color);

	UStaticMeshComponent* Strip = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
	Strip->SetStaticMesh(BeamMesh);
	Strip->SetMaterial(0, Material);
	Strip->SetMobility(EComponentMobility::Movable);
	Strip->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Strip->SetCastShadow(false);
	Strip->SetCanEverAffectNavigation(false);
	Strip->SetGenerateOverlapEvents(false);
	// Retail vertices sit at x = +-width, so the strip is twice the stored width across.
	Strip->SetRelativeScale3D(FVector(2.f * Beam.Width * Scale * SWGWorldScale / 100.f, 1.f, Beam.Length * Scale * SWGWorldScale / 100.f));
	Strip->SetupAttachment(this);
	Strip->RegisterComponent();
	Render = Strip;
	SetComponentTickEnabled(true);
	return true;
}

bool USWGAppearanceBillboardComponent::ResolveViewerLocation(FVector& OutLocation) const
{
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (!Camera)
	{
		return false;
	}
	OutLocation = Camera->GetCameraLocation();
	return true;
}

void USWGAppearanceBillboardComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	FVector Viewer;
	if (!bBeam || !Render || !ResolveViewerLocation(Viewer))
	{
		return;
	}

	// The beam runs along sim +Z = UE +X; the strip mesh runs along its own Z with Y as normal, so turn it about the beam axis to face the viewer.
	const FTransform ParentTransform = GetComponentTransform();
	const float HalfLength = Render->GetRelativeScale3D().Z * 50.f;
	const FVector ViewerLocal = ParentTransform.InverseTransformPosition(Viewer) - FVector(HalfLength, 0.f, 0.f);
	const FVector Normal = ComputeBeamNormal(FVector::XAxisVector, ViewerLocal);
	Render->SetRelativeRotation(FRotationMatrix::MakeFromZY(FVector::XAxisVector, Normal).ToQuat());
}
