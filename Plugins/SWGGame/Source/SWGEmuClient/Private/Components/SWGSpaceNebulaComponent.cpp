#include "Components/SWGSpaceNebulaComponent.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Objects/World/SWGShip.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGNebulaReader.h"
#include "Common/SWGWorldScale.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

namespace
{
	TAutoConsoleVariable<int32> CVarNebulaQuality(
		TEXT("swg.Space.NebulaQuality"), 3,
		TEXT("Gas cloud quality 0-3: volume resolution, step size, render downsample and self-shadowing. 2 is about 4 ms at 3840x2160 on an RTX 2070; 3 is for high-end GPUs. swg.Space.Quality sets it."));

	TAutoConsoleVariable<bool> CVarNebulaWisps(
		TEXT("swg.Space.NebulaWisps"), true,
		TEXT("Close-range gas wisps around the camera inside a nebula. Next zone load."));

	constexpr int32 QualityCount = 4;
	// Per tier 0-3: nearby volume resolution; far volumes use half (see GetVolumeResolution).
	constexpr int32 NearResolution[QualityCount] = {32, 48, 64, 64};
	constexpr float StepFactor[QualityCount] = {3.f, 2.f, 1.5f, 1.f};
	constexpr float LightingDownsample[QualityCount] = {4.f, 3.f, 2.f, 2.f};

	struct FRenderSetting { const TCHAR* Name; const TCHAR* Values[QualityCount]; };
	// The ray marcher's global limits per tier; downsample renders the volumes at 1/N resolution per axis and upsamples.
	const FRenderSetting RenderSettings[] = {
		{TEXT("r.HeterogeneousVolumes.DownsampleFactor"), {TEXT("4"), TEXT("4"), TEXT("3"), TEXT("2")}},
		{TEXT("r.HeterogeneousVolumes.MaxStepCount"), {TEXT("64"), TEXT("128"), TEXT("128"), TEXT("128")}},
	};

	// A volume beyond this many radii gets the far LOD; hysteresis stops it flipping at the boundary.
	constexpr double FarLodRadii = 3.0;
	// Camera depth, as a fraction of the radius, over which the inside-cloud effects fade in.
	constexpr float InsideRampDepth = 0.3f;
	constexpr float StarDimAmount = 0.9f;
	// Extra in-scatter on the zone lights, which only the gas uses, so the lit side reads against the skybox and emission.
	constexpr float SunScatterIntensity = 3.f;
	constexpr int32 WispCount = 90;
	// Wisps are a hint of passing gas, not a second fog layer.
	constexpr float WispOpacity = .35f;
	constexpr double WispHalfExtent = 2000.0;
	constexpr int32 FlashLightCount = 3;
	// Flashes are only simulated when the camera is this many radii from the nebula.
	constexpr double LightningRadii = 2.0;

	float ClampedQuality() { return float(FMath::Clamp(CVarNebulaQuality.GetValueOnGameThread(), 0, QualityCount-1)); }
}

FIntVector USWGSpaceNebulaComponent::GetVolumeResolution(int32 Quality, bool bFarLod)
{
	const int32 Near = NearResolution[FMath::Clamp(Quality, 0, QualityCount-1)];
	return FIntVector(bFarLod ? FMath::Max(Near/2, 16) : Near);
}

bool USWGSpaceNebulaComponent::Setup(USWGTreSubsystem* TreSubsystem, const FString& TerrainVirtualPath)
{
	Nebulas.Reset(); Flashes.Reset(); AppliedQuality = INDEX_NONE; InsideFactor = 0; InsideIndex = INDEX_NONE; BaseStarIntensity = -1; bStarsDimmed = false; bLightScatterApplied = false;
	const FString NebulaPath = TEXT("datatables/space/nebula/")+FPaths::GetBaseFilename(TerrainVirtualPath)+TEXT(".iff");
	if (!TreSubsystem || !TreSubsystem->FileExists(NebulaPath)) return true;
	FSWGDataTableData Table; TArray<FSWGNebulaData> NebulaRows;
	if (!FSWGDataTableReader::ReadDataTable(TreSubsystem->CreateIffReader(NebulaPath),Table) || !FSWGNebulaReader::Read(Table,NebulaRows)) { UE_LOG(LogTemp, Warning, TEXT("USWGSpaceNebulaComponent: cannot read %s"), *NebulaPath); return false; }
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/SWGEmu/Materials/M_SWGSpaceNebula.M_SWGSpaceNebula"));
	if (!Material) { UE_LOG(LogTemp, Warning, TEXT("USWGSpaceNebulaComponent: M_SWGSpaceNebula is missing; run swg.CreateSpaceNebulaAssets")); return false; }
	const int32 Quality = int32(ClampedQuality());
	int32 RowIndex = -1;
	for (const FSWGNebulaData& Nebula : NebulaRows)
	{
		++RowIndex;
		if (Nebula.Density <= 0) continue;
		AActor* Actor = GetWorld()->SpawnActor<AActor>();
		if (!Actor) return false;
		Actor->Tags.Add(TEXT("SWGPlanetLighting"));
		UHeterogeneousVolumeComponent* Volume = NewObject<UHeterogeneousVolumeComponent>(Actor);
		Actor->SetRootComponent(Volume); Volume->SetMobility(EComponentMobility::Movable);
		Volume->bPivotAtCentroid = true;
		Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision); Volume->SetCastShadow(false);
		Volume->ComponentTags.Add(TEXT("SWGSpaceNebula"));
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material,Actor);
		const FLinearColor Tint = FMath::Lerp(Nebula.OrientedColor,Nebula.FacingColor,Nebula.FacingPercent);
		// The native ray marcher integrates density over local voxel units, including component scale itself.
		MID->SetScalarParameterValue(TEXT("Density"),Nebula.Density*Tint.A*3/32);
		MID->SetVectorParameterValue(TEXT("OrientedColor"),Nebula.OrientedColor);
		MID->SetVectorParameterValue(TEXT("FacingColor"),Nebula.FacingColor);
		MID->SetScalarParameterValue(TEXT("FacingBlend"),Nebula.FacingPercent);
		// The noise volume tiles, so a per-row offset makes neighbouring clouds differ at no cost.
		FRandomStream SeedStream(int32(GetTypeHash(Nebula.Name))+RowIndex*7919);
		MID->SetVectorParameterValue(TEXT("Seed"),FLinearColor(SeedStream.FRand(),SeedStream.FRand(),SeedStream.FRand(),0));
		const FVector Native = Nebula.NativePosition;
		const FVector Position = FVector(Native.Z,Native.X,Native.Y)*SWGWorldScale;
		const double Radius = Nebula.Radius*SWGWorldScale;
		const auto* Graph = GetWorld()->GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>();
		const FTransform Frame = Graph ? Graph->GetSpaceFrame() : FTransform::Identity;
		// The native proxy caches its voxel step size at registration, so register at the final world scale.
		Volume->SetWorldTransform(FTransform(Frame.GetRotation().Inverse(),Frame.InverseTransformPosition(Position),FVector(Radius/32)));
		Volume->SetMaterial(0,MID); Volume->RegisterComponent();
		FNebulaVisual Visual{Volume,Position,Radius,MID};
		Visual.Tint = Tint; Visual.Density = Nebula.Density;
		Visual.LightningFrequency = Nebula.LightningFrequency; Visual.LightningDurationMax = Nebula.LightningDurationMax;
		Visual.LightningColor = Nebula.LightningColor; Visual.LightningRampColor = Nebula.LightningRampColor;
		ApplyVolumeSettings(Visual,Quality);
		Nebulas.Add(Visual);
	}
	if (Nebulas.IsEmpty()) return true;

	// A second unbound volume at higher priority, fading in with depth, rather than editing the scene's own volume.
	InsideVolume = GetWorld()->SpawnActor<APostProcessVolume>();
	if (!InsideVolume) return false;
	InsideVolume->Tags.Add(TEXT("SWGPlanetLighting"));
	InsideVolume->bUnbound = true; InsideVolume->Priority = 10; InsideVolume->BlendWeight = 0; InsideVolume->bEnabled = false;
	InsideVolume->Settings.bOverride_SceneColorTint = true;

	AActor* SkyActor = GetOwner();
	UStaticMesh* Quad = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* WispParent = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/SWGEmu/Materials/M_SWGSpaceWisp.M_SWGSpaceWisp"));
	const bool bWispsEnabled = CVarNebulaWisps.GetValueOnGameThread();
	if (bWispsEnabled && SkyActor && Quad && WispParent)
	{
		Wisps = NewObject<UInstancedStaticMeshComponent>(SkyActor,TEXT("SWGNebulaWisps"));
		Wisps->SetupAttachment(SkyActor->GetRootComponent());
		Wisps->SetAbsolute(true,true,true); Wisps->SetMobility(EComponentMobility::Movable);
		Wisps->SetStaticMesh(Quad); Wisps->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wisps->SetCastShadow(false); Wisps->bAffectDistanceFieldLighting = false; Wisps->bDisableCollision = true;
		// The material is translucent with world-position offset, which Nanite cannot draw.
		Wisps->bDisallowNanite = true;
		WispMaterial = UMaterialInstanceDynamic::Create(WispParent,SkyActor);
		Wisps->SetMaterial(0,WispMaterial);
		Wisps->RegisterComponent();
		FRandomStream WispStream(int32(GetTypeHash(TerrainVirtualPath)));
		WispOffsets.Reset();
		for (int32 Index = 0; Index < WispCount; ++Index)
		{
			WispOffsets.Add(FVector(WispStream.FRandRange(-1,1),WispStream.FRandRange(-1,1),WispStream.FRandRange(-1,1))*WispHalfExtent);
			Wisps->AddInstance(FTransform(FRotator(0,0,0),FVector::ZeroVector,FVector(0.0)),true);
		}
		Wisps->SetVisibility(false);
	}
	else if (bWispsEnabled) UE_LOG(LogTemp, Warning, TEXT("USWGSpaceNebulaComponent: M_SWGSpaceWisp is missing; run swg.CreateSpaceNebulaAssets. Close-range wisps are off."));
	return true;
}

void USWGSpaceNebulaComponent::ApplyVolumeSettings(FNebulaVisual& Nebula, int32 Quality)
{
	const float FarScale = Nebula.bFar ? 2.f : 1.f;
	Nebula.Volume->StepFactor = StepFactor[Quality];
	Nebula.Volume->LightingDownsampleFactor = LightingDownsample[Quality]*FarScale;
	Nebula.Volume->SetVolumeResolution(GetVolumeResolution(Quality,Nebula.bFar));
	Nebula.Volume->MarkRenderStateDirty();
	// The renderer's own self-shadow ray is a fixed 100 m, useless at kilometre scale, so shadowing is a material term; far volumes and tier 0 go without.
	if (IsValid(Nebula.Material)) Nebula.Material->SetScalarParameterValue(TEXT("ShadowStrength"), Quality > 0 && !Nebula.bFar ? 1.f : 0.f);
}

void USWGSpaceNebulaComponent::ApplyQuality()
{
	const int32 Quality = int32(ClampedQuality());
	if (Quality == AppliedQuality) return;
	AppliedQuality = Quality;
	IConsoleManager& Console = IConsoleManager::Get();
	for (const FRenderSetting& Setting : RenderSettings)
	{
		IConsoleVariable* Variable = Console.FindConsoleVariable(Setting.Name);
		if (!Variable) continue;
		OriginalRenderSettings.FindOrAdd(Setting.Name, Variable->GetString());
		Variable->Set(Setting.Values[Quality], ECVF_SetByCode);
	}
	for (FNebulaVisual& Nebula : Nebulas)
	{
		if (!IsValid(Nebula.Volume)) continue;
		ApplyVolumeSettings(Nebula,Quality);
	}
}

void USWGSpaceNebulaComponent::ApplyDistanceLod(FNebulaVisual& Nebula, double Distance)
{
	const double Boundary = Nebula.Radius*FarLodRadii;
	const bool bFar = Nebula.bFar ? Distance > Boundary*0.9 : Distance > Boundary*1.1;
	if (bFar == Nebula.bFar) return;
	Nebula.bFar = bFar;
	const int32 Quality = AppliedQuality == INDEX_NONE ? int32(ClampedQuality()) : AppliedQuality;
	// Far volumes get half the resolution, which also halves their march steps, a coarser lighting cache and no self-shadow term.
	ApplyVolumeSettings(Nebula,Quality);
}

void USWGSpaceNebulaComponent::Update(const FVector& Viewpoint, const TArray<TObjectPtr<ADirectionalLight>>& Lights, float DeltaTime)
{
	const USWGObjectGraphSubsystem* Graph = GetWorld()->GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>();
	const FTransform Frame = Graph ? Graph->GetSpaceFrame() : FTransform::Identity;
	if (Nebulas.IsEmpty()) return;
	ApplyQuality();
	if (!bLightScatterApplied && !Lights.IsEmpty())
	{
		for (const TObjectPtr<ADirectionalLight>& Light : Lights) if (IsValid(Light)) Light->GetComponent()->SetVolumetricScatteringIntensity(SunScatterIntensity);
		bLightScatterApplied = true;
	}
	const FVector Camera = Frame.TransformPosition(Viewpoint);
	const FVector LightTravel = Lights.IsEmpty() || !IsValid(Lights[0]) ? FVector::ZeroVector : Lights[0]->GetActorForwardVector();
	for (FNebulaVisual& Nebula : Nebulas)
	{
		if (!IsValid(Nebula.Volume)) continue;
		Nebula.Volume->SetWorldTransform(FTransform(Frame.GetRotation().Inverse(),
			Frame.InverseTransformPosition(Nebula.SpacePosition),FVector(Nebula.Radius/32)));
		ApplyDistanceLod(Nebula, FVector::Dist(Camera, Nebula.SpacePosition));
		if (IsValid(Nebula.Material)) Nebula.Material->SetVectorParameterValue(TEXT("LightDirection"), FLinearColor(LightTravel.X, LightTravel.Y, LightTravel.Z, 0));
	}
	UpdateInsideCloud(Camera);
	UpdateWisps(Frame, Camera);
	UpdateLightning(Camera, DeltaTime);
}

void USWGSpaceNebulaComponent::UpdateInsideCloud(const FVector& Camera)
{
	InsideFactor = 0; InsideIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Nebulas.Num(); ++Index)
	{
		const FNebulaVisual& Nebula = Nebulas[Index];
		const float Depth = 1.f - float(FVector::Dist(Camera, Nebula.SpacePosition)/Nebula.Radius);
		const float Factor = FMath::SmoothStep(0.f, InsideRampDepth, Depth) * FMath::Clamp(Nebula.Density, 0.f, 1.f);
		if (Factor > InsideFactor) { InsideFactor = Factor; InsideIndex = Index; }
	}
	if (IsValid(InsideVolume))
	{
		InsideVolume->bEnabled = InsideFactor > 0;
		InsideVolume->BlendWeight = InsideFactor;
		if (InsideIndex != INDEX_NONE)
		{
			// Tint toward the cloud's own colour at constant brightness: scale to the brightest channel, then halve the strength.
			const FLinearColor Colour = Nebulas[InsideIndex].Tint;
			const float Peak = FMath::Max3(Colour.R, Colour.G, Colour.B);
			const FLinearColor Normalised = Peak > UE_SMALL_NUMBER ? Colour/Peak : FLinearColor::White;
			InsideVolume->Settings.SceneColorTint = FMath::Lerp(FLinearColor::White, Normalised, .5f);
		}
	}

	if (!StarMaterial.IsValid() && GetOwner())
	{
		TArray<UStaticMeshComponent*> Meshes; GetOwner()->GetComponents(Meshes);
		for (UStaticMeshComponent* Mesh : Meshes)
			if (Mesh->GetFName() == FName(TEXT("SWGStarDome"))) StarMaterial = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	}
	if (UMaterialInstanceDynamic* Stars = StarMaterial.Get())
	{
		// The scene sets the star intensity once; read it before the first dim so leaving the cloud restores it exactly.
		if (!bStarsDimmed) Stars->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Intensity")), BaseStarIntensity);
		if (InsideFactor > 0 || bStarsDimmed)
		{
			Stars->SetScalarParameterValue(TEXT("Intensity"), BaseStarIntensity*(1.f-StarDimAmount*InsideFactor));
			bStarsDimmed = InsideFactor > 0;
		}
	}
}

void USWGSpaceNebulaComponent::UpdateWisps(const FTransform& Frame, const FVector& Camera)
{
	if (!IsValid(Wisps) || WispOffsets.IsEmpty()) return;
	Wisps->SetVisibility(InsideFactor > 0.001f);
	if (InsideFactor <= 0.001f) return;
	if (IsValid(WispMaterial) && InsideIndex != INDEX_NONE)
	{
		WispMaterial->SetScalarParameterValue(TEXT("Opacity"), InsideFactor*WispOpacity);
		WispMaterial->SetVectorParameterValue(TEXT("Tint"), Nebulas[InsideIndex].Tint);
	}
	FBox ShipBounds(ForceInit);
	const USWGObjectGraphSubsystem* Graph = GetWorld()->GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>();
	if (ASWGShip* Ship = Graph ? Graph->GetLocalPlayerShip() : nullptr)
	{
		FVector Center, Extent; Ship->GetActorBounds(false, Center, Extent);
		if (!Extent.IsNearlyZero()) ShipBounds = FBox(Center-Extent, Center+Extent);
	}
	FRandomStream SizeStream(17);
	TArray<FTransform> Transforms; Transforms.Reserve(WispOffsets.Num());
	const double Span = WispHalfExtent*2;
	for (const FVector& Offset : WispOffsets)
	{
		// Offsets are fixed in absolute space, wrapped into the box around the camera so the field never runs out.
		FVector Relative = Offset-Camera;
		Relative.X -= Span*FMath::FloorToDouble((Relative.X+WispHalfExtent)/Span);
		Relative.Y -= Span*FMath::FloorToDouble((Relative.Y+WispHalfExtent)/Span);
		Relative.Z -= Span*FMath::FloorToDouble((Relative.Z+WispHalfExtent)/Span);
		const FVector WorldPosition = Frame.InverseTransformPosition(Camera+Relative);
		// shortcut: hull bounds keep wisps out of POB rooms, like the dust field.
		const bool bInsideHull = ShipBounds.IsValid && ShipBounds.IsInside(WorldPosition);
		const double Size = bInsideHull ? 0.0 : SizeStream.FRandRange(3.0, 8.0);
		Transforms.Add(FTransform(FQuat::Identity, WorldPosition, FVector(Size)));
	}
	Wisps->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
}

bool USWGSpaceNebulaComponent::TriggerLightning(int32 NebulaIndex)
{
	if (!Nebulas.IsValidIndex(NebulaIndex) || Nebulas[NebulaIndex].LightningFrequency <= 0 || Nebulas[NebulaIndex].LightningDurationMax <= 0) return false;
	AActor* SkyActor = GetOwner();
	if (!SkyActor) return false;
	int32 Slot = INDEX_NONE;
	for (int32 Index = 0; Index < FlashLights.Num() && Slot == INDEX_NONE; ++Index)
		if (!Flashes.ContainsByPredicate([Index](const FFlash& Flash) { return Flash.LightIndex == Index; })) Slot = Index;
	if (Slot == INDEX_NONE && FlashLights.Num() < FlashLightCount)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(SkyActor);
		Light->SetupAttachment(SkyActor->GetRootComponent());
		Light->SetAbsolute(true,true,true); Light->SetMobility(EComponentMobility::Movable);
		Light->SetCastShadows(false); Light->bUseInverseSquaredFalloff = false; Light->SetLightFalloffExponent(2.f);
		Light->SetIntensity(0); Light->SetVisibility(false); Light->RegisterComponent();
		Slot = FlashLights.Add(Light);
	}
	if (Slot == INDEX_NONE) return false;
	const FNebulaVisual& Nebula = Nebulas[NebulaIndex];
	FFlash Flash;
	Flash.NebulaIndex = NebulaIndex; Flash.LightIndex = Slot;
	Flash.Duration = Random.FRandRange(Nebula.LightningDurationMax*.1f, Nebula.LightningDurationMax);
	// Inside the volume, away from its fading edge.
	const FVector SpaceSpot = Nebula.SpacePosition + Random.VRand()*Random.FRand()*Nebula.Radius*.5;
	const USWGObjectGraphSubsystem* Graph = GetWorld()->GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>();
	UPointLightComponent* Light = FlashLights[Slot];
	Light->SetWorldLocation((Graph ? Graph->GetSpaceFrame() : FTransform::Identity).InverseTransformPosition(SpaceSpot));
	Light->SetAttenuationRadius(float(Nebula.Radius*.6));
	Light->SetLightColor(Nebula.LightningColor); Light->SetVisibility(true);
	Flashes.Add(Flash);
	return true;
}

void USWGSpaceNebulaComponent::UpdateLightning(const FVector& Camera, float DeltaTime)
{
	// Chance per frame of a new flash is the authored flashes-per-second rate; retail's exact meaning of the column is inferred.
	for (int32 Index = 0; Index < Nebulas.Num(); ++Index)
	{
		const FNebulaVisual& Nebula = Nebulas[Index];
		if (Nebula.LightningFrequency <= 0 || FVector::Dist(Camera, Nebula.SpacePosition) > Nebula.Radius*LightningRadii) continue;
		if (Random.FRand() < Nebula.LightningFrequency*DeltaTime) TriggerLightning(Index);
	}
	for (int32 FlashIndex = Flashes.Num()-1; FlashIndex >= 0; --FlashIndex)
	{
		FFlash& Flash = Flashes[FlashIndex];
		Flash.Age += DeltaTime;
		UPointLightComponent* Light = FlashLights.IsValidIndex(Flash.LightIndex) ? FlashLights[Flash.LightIndex].Get() : nullptr;
		const float Progress = Flash.Age/FMath::Max(Flash.Duration, UE_SMALL_NUMBER);
		if (!Light || Progress >= 1.f || !Nebulas.IsValidIndex(Flash.NebulaIndex))
		{
			if (Light) { Light->SetIntensity(0); Light->SetVisibility(false); }
			Flashes.RemoveAtSwap(FlashIndex);
			continue;
		}
		const FNebulaVisual& Nebula = Nebulas[Flash.NebulaIndex];
		// Fast decay with a flicker, shifting from the strike colour to the ramp colour as it dies.
		const float Envelope = FMath::Square(1.f-Progress) * (.65f + .35f*FMath::Sin(Flash.Age*45.f));
		Light->SetIntensity(40.f*Envelope);
		Light->SetLightColor(FMath::Lerp(Nebula.LightningColor, Nebula.LightningRampColor, Progress));
	}
}

void USWGSpaceNebulaComponent::Teardown()
{
	IConsoleManager& Console = IConsoleManager::Get();
	for (const TPair<FString, FString>& Original : OriginalRenderSettings)
		if (IConsoleVariable* Variable = Console.FindConsoleVariable(*Original.Key)) Variable->Set(*Original.Value, ECVF_SetByCode);
	OriginalRenderSettings.Reset(); AppliedQuality = INDEX_NONE;
	for (const FNebulaVisual& Nebula : Nebulas)
		if (IsValid(Nebula.Volume) && IsValid(Nebula.Volume->GetOwner())) Nebula.Volume->GetOwner()->Destroy();
	Nebulas.Reset(); Flashes.Reset();
	if (IsValid(InsideVolume)) InsideVolume->Destroy();
	InsideVolume = nullptr;
	if (IsValid(Wisps)) Wisps->DestroyComponent();
	Wisps = nullptr;
	for (UPointLightComponent* Light : FlashLights) if (IsValid(Light)) Light->DestroyComponent();
	FlashLights.Reset();
}
