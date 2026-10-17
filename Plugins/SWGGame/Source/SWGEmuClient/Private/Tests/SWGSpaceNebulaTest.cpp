#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "ImageUtils.h"
#include "TextureResource.h"
#include "SceneInterface.h"
#include "MaterialShared.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Components/SWGSpaceNebulaComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceNebulaGasTest, "SWG.Space.NebulaGasV2",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

bool FSWGSpaceNebulaGasTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->InitializeStandalone();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) { Game->Shutdown(); return false; }
	Game->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(Game);
	ON_SCOPE_EXIT { Game->Shutdown(); World->DestroyWorld(false); };
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	USWGTerrainSubsystem* Terrain = Game->GetSubsystem<USWGTerrainSubsystem>();
	if (!Tre || !Terrain) return false;
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives load"), Tre->LoadArchives())) return false;
	// The scene's quality tier and the sweeps below change global renderer settings; saved before the scene
	// loads so later tests see the real defaults.
	TMap<FString, FString> SavedSettings;
	for (const TCHAR* Name : {TEXT("r.HeterogeneousVolumes.DownsampleFactor"), TEXT("r.HeterogeneousVolumes.MaxStepCount"), TEXT("r.HeterogeneousVolumes.Shadows"), TEXT("r.HeterogeneousVolumes.LightType.Directional"), TEXT("r.HeterogeneousVolumes.LightingCache.UseAVSM"), TEXT("r.HeterogeneousVolumes.Shadows.Type")})
		SavedSettings.Add(Name, IConsoleManager::Get().FindConsoleVariable(Name)->GetString());
	ON_SCOPE_EXIT { for (const TPair<FString, FString>& Saved : SavedSettings) IConsoleManager::Get().FindConsoleVariable(*Saved.Key)->Set(*Saved.Value, ECVF_SetByCode); };
	Terrain->BeginLoadTerrain(TEXT("terrain/space_tatooine.trn"), FVector::ZeroVector);
	USWGSpaceNebulaComponent* Gas = Terrain->SpaceNebula;
	if (!TestNotNull(TEXT("Tatooine space scene has a nebula component"), Gas) || !TestTrue(TEXT("Tatooine nebulas spawned"), Gas->Nebulas.Num() > 1)) return false;
	Terrain->Tick(1.f/60.f);
	const TArray<TObjectPtr<ADirectionalLight>>& Lights = Terrain->SpaceLights;
	TestEqual(TEXT("both zone directional lights drive the gas"), Lights.Num(), 2);
	// The frame is the identity here (no ship), so world and absolute space coordinates agree.
	const int32 MainIndex = 0;
	const USWGSpaceNebulaComponent::FNebulaVisual& Main = Gas->Nebulas[MainIndex];
	const FVector Centre = Main.SpacePosition;
	const double Radius = Main.Radius;
	const FVector Direction = Centre.GetSafeNormal();

	UMaterialInstanceDynamic* Stars = nullptr;
	for (TActorIterator<AActor> It(World); It && !Stars; ++It)
	{
		TArray<UStaticMeshComponent*> Meshes; It->GetComponents(Meshes);
		for (UStaticMeshComponent* Mesh : Meshes)
			if (Mesh->GetFName() == FName(TEXT("SWGStarDome"))) Stars = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	}
	if (!TestNotNull(TEXT("star dome material available"), Stars)) return false;
	auto StarIntensity = [Stars] { float Value = -1; Stars->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Intensity")), Value); return Value; };
	const float BaseStars = StarIntensity();
	TestTrue(TEXT("stars are lit before entering a cloud"), BaseStars > 0);

	// Outside, at the edge and deep inside: the tint, star dimming and wisps follow the camera.
	Gas->Update(Centre-Direction*Radius*3.0, Lights, 1.f/60.f);
	TestEqual(TEXT("no inside effect outside every nebula"), Gas->GetInsideFactor(), 0.f);
	TestFalse(TEXT("inside tint volume disabled outside"), Gas->InsideVolume->bEnabled);
	TestEqual(TEXT("inside tint weight is zero outside"), Gas->InsideVolume->BlendWeight, 0.f);
	TestEqual(TEXT("stars keep full brightness outside"), StarIntensity(), BaseStars);
	TestFalse(TEXT("wisps hidden outside"), Gas->Wisps && Gas->Wisps->IsVisible());
	Gas->Update(Centre+FVector(.1,.1,.1)*Radius*.1, Lights, 1.f/60.f);
	TestTrue(TEXT("deep inside the densest nebula the effect is nearly full"), Gas->GetInsideFactor() > .9f);
	TestTrue(TEXT("inside tint volume enabled"), Gas->InsideVolume->bEnabled);
	TestEqual(TEXT("inside tint weight is the inside factor"), Gas->InsideVolume->BlendWeight, Gas->GetInsideFactor());
	TestTrue(TEXT("inside tint shifts the scene colour"), !Gas->InsideVolume->Settings.SceneColorTint.Equals(FLinearColor::White, .01f));
	TestTrue(TEXT("stars dimmed inside"), StarIntensity() < BaseStars*.25f);
	const bool bWispsEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.NebulaWisps"))->GetBool();
	TestEqual(TEXT("wisps exist exactly when enabled"), Gas->Wisps != nullptr, bWispsEnabled);
	if (Gas->Wisps)
	{
		TestTrue(TEXT("wisps visible inside"), Gas->Wisps->IsVisible());
		int32 Placed = 0;
		for (int32 Instance = 0; Instance < Gas->Wisps->GetInstanceCount(); ++Instance)
		{
			FTransform Transform; Gas->Wisps->GetInstanceTransform(Instance, Transform, true);
			if (Transform.GetScale3D().X > .1) { ++Placed; TestTrue(TEXT("wisps stay in the box around the camera"), (Transform.GetLocation()-(Centre+FVector(.1,.1,.1)*Radius*.1)).GetAbsMax() <= 2001.0); }
		}
		TestTrue(TEXT("most wisps are placed around the camera"), Placed > Gas->Wisps->GetInstanceCount()/2);
	}
	Gas->Update(Centre+Direction*Radius*.97, Lights, 1.f/60.f);
	const float EdgeFactor = Gas->GetInsideFactor();
	TestTrue(TEXT("the edge of a cloud is only partly inside"), EdgeFactor > 0 && EdgeFactor < .5f);
	Gas->Update(Centre-Direction*Radius*3.0, Lights, 1.f/60.f);
	TestEqual(TEXT("inside effect removed again outside"), Gas->GetInsideFactor(), 0.f);
	TestEqual(TEXT("star brightness restored outside"), StarIntensity(), BaseStars);
	TestFalse(TEXT("inside tint volume disabled again"), Gas->InsideVolume->bEnabled);
	TestFalse(TEXT("wisps hidden again"), Gas->Wisps && Gas->Wisps->IsVisible());

	// Distance LOD and quality tiers.
	const int32 OriginalQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.NebulaQuality"))->GetInt();
	ON_SCOPE_EXIT { IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.NebulaQuality"))->Set(OriginalQuality, ECVF_SetByCode); };
	IConsoleVariable* QualityVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.NebulaQuality"));
	for (int32 Quality = 0; Quality < 4; ++Quality)
	{
		QualityVariable->Set(Quality, ECVF_SetByCode);
		Gas->Update(Centre, Lights, 1.f/60.f);
		TestEqual(TEXT("near volume uses the tier resolution"), Main.Volume->VolumeResolution, USWGSpaceNebulaComponent::GetVolumeResolution(Quality, false));
		float ShadowStrength = -1; Main.Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ShadowStrength")), ShadowStrength);
		TestEqual(TEXT("near gas self-shadows except on the cheapest tier"), ShadowStrength, Quality > 0 ? 1.f : 0.f);
		Gas->Update(Centre-Direction*Radius*5.0, Lights, 1.f/60.f);
		TestTrue(TEXT("distant volume drops to the far LOD"), Main.bFar);
		Main.Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ShadowStrength")), ShadowStrength);
		TestEqual(TEXT("far gas goes without self-shadow"), ShadowStrength, 0.f);
		TestEqual(TEXT("far volume uses the half resolution"), Main.Volume->VolumeResolution, USWGSpaceNebulaComponent::GetVolumeResolution(Quality, true));
		TestTrue(TEXT("far resolution is no higher than near"), Main.Volume->VolumeResolution.X <= USWGSpaceNebulaComponent::GetVolumeResolution(Quality, false).X);
		TestEqual(TEXT("downsample setting follows the tier"), IConsoleManager::Get().FindConsoleVariable(TEXT("r.HeterogeneousVolumes.DownsampleFactor"))->GetInt(), TArray<int32>{4, 4, 3, 2}[Quality]);
	}
	QualityVariable->Set(OriginalQuality, ECVF_SetByCode);
	Gas->Update(Centre, Lights, 1.f/60.f);
	TestFalse(TEXT("back inside the LOD radius the volume returns to near LOD"), Main.bFar);

	// Lightning: authored nebulas flash a light inside the volume that then dies away.
	int32 QuietIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Gas->Nebulas.Num(); ++Index) if (Gas->Nebulas[Index].LightningFrequency <= 0) QuietIndex = Index;
	if (TestTrue(TEXT("fixture has a nebula without lightning"), QuietIndex != INDEX_NONE))
		TestFalse(TEXT("a nebula with no authored lightning never flashes"), Gas->TriggerLightning(QuietIndex));
	TestTrue(TEXT("authored lightning starts a flash"), Gas->TriggerLightning(MainIndex));
	if (TestEqual(TEXT("one flash light created"), Gas->FlashLights.Num(), 1))
	{
		UPointLightComponent* Flash = Gas->FlashLights[0];
		TestTrue(TEXT("flash light is on"), Flash->IsVisible());
		TestTrue(TEXT("flash is inside the nebula"), FVector::Dist(Flash->GetComponentLocation(), Centre) < Radius*.5+1);
		const FLinearColor FlashColor = Flash->GetLightColor();
		TestTrue(TEXT("flash starts in the authored colour"), FMath::Abs(FlashColor.R-Main.LightningColor.R) < .1f && FMath::Abs(FlashColor.G-Main.LightningColor.G) < .1f && FMath::Abs(FlashColor.B-Main.LightningColor.B) < .1f);
		Gas->Update(Centre, Lights, .01f);
		TestTrue(TEXT("flash has intensity while it lasts"), Flash->Intensity > 0);
		Gas->Update(Centre, Lights, Main.LightningDurationMax+1.f);
		TestFalse(TEXT("flash light off after its duration"), Flash->IsVisible());
	}

	// Rendering: captures from outside, at the edge and inside a real Tatooine nebula.
	AActor* CaptureActor = World->SpawnActor<AActor>();
	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(CaptureActor);
	CaptureActor->SetRootComponent(Capture); Capture->RegisterComponent();
	Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false; Capture->FOVAngle = 80;
	Capture->ShowFlags.SetHeterogeneousVolumes(true);
	Capture->ShowFlags.SetAntiAliasing(false); Capture->ShowFlags.SetMotionBlur(false); Capture->ShowFlags.SetGrain(false);
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	// Scene captures disable the before-DOF translucency pass the sky uses unless asked.
	Capture->ShowFlags.SetSeparateTranslucency(true);
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true; Capture->PostProcessSettings.AutoExposureBias = 0;
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(CaptureActor);
	Target->InitCustomFormat(1280, 720, PF_B8G8R8A8, false); Capture->TextureTarget = Target;
#if WITH_EDITOR
	TSet<FMaterialResource*> PreparedResources;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UPrimitiveComponent*> Components; It->GetComponents(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			TArray<UMaterialInterface*> Materials; Component->GetUsedMaterials(Materials);
			for (UMaterialInterface* UsedMaterial : Materials)
			{
				FMaterialResource* UsedResource = UsedMaterial ? UsedMaterial->GetMaterialResource(World->Scene->GetShaderPlatform()) : nullptr;
				// Recaching a complete shader map leaves it incomplete for this and later tests; only compile what is missing.
				if (UsedResource && !PreparedResources.Contains(UsedResource) && !UsedResource->IsGameThreadShaderMapComplete())
				{
					UsedResource->CacheShaders(EMaterialShaderPrecompileMode::Synchronous); PreparedResources.Add(UsedResource);
				}
			}
		}
	}
	FAssetCompilingManager::Get().FinishAllCompilation();
#endif
	const FString Directory = FPaths::ProjectSavedDir()/TEXT("Automation/SpaceWindows");
	IFileManager::Get().MakeDirectory(*Directory, true);
	auto Snap = [&](const TCHAR* Name, const FVector& Location, const FVector& LookDirection, TArray<FColor>& Pixels)
	{
		Capture->SetWorldLocation(Location); Capture->SetWorldRotation(LookDirection.Rotation());
		Gas->Update(Location, Lights, 1.f/60.f);
		World->SendAllEndOfFrameUpdates(); Capture->CaptureScene();
		if (!TestTrue(TEXT("render target pixels readable"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return;
		TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(Target->SizeX, Target->SizeY, Pixels, Png);
		TestTrue(TEXT("capture saved"), FFileHelper::SaveArrayToFile(Png, *(Directory/Name)));
	};
	auto GasVisible = [&](bool bVisible) { for (const auto& Nebula : Gas->Nebulas) Nebula.Volume->SetVisibility(bVisible); };
	auto Changed = [](const TArray<FColor>& First, const TArray<FColor>& Second)
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < FMath::Min(First.Num(), Second.Num()); ++Index)
			Count += FMath::Abs(int32(First[Index].R)-int32(Second[Index].R)) > 20 || FMath::Abs(int32(First[Index].G)-int32(Second[Index].G)) > 20
				|| FMath::Abs(int32(First[Index].B)-int32(Second[Index].B)) > 20;
		return Count;
	};
	auto MeanLuminance = [](const TArray<FColor>& Pixels)
	{
		double Sum = 0;
		for (const FColor& Pixel : Pixels) Sum += .2126*Pixel.R + .7152*Pixel.G + .0722*Pixel.B;
		return Pixels.IsEmpty() ? 0.0 : Sum/Pixels.Num();
	};
	const FVector Sideways = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
	struct FView { const TCHAR* Label; FVector Location; FVector Look; };
	const FView Views[] = {
		{TEXT("outside"), Centre-Direction*Radius*3.2+Sideways*Radius*.6, (Centre-(Centre-Direction*Radius*3.2+Sideways*Radius*.6)).GetSafeNormal()},
		{TEXT("edge"), Centre+Sideways*Radius*.93, (-Sideways+Direction*.3).GetSafeNormal()},
		{TEXT("inside"), Centre+Sideways*Radius*.25, (Direction+Sideways*.4).GetSafeNormal()},
	};
	for (const FView& View : Views)
	{
		TArray<FColor> WithGas, WithoutGas;
		GasVisible(true);
		Snap(*FString::Printf(TEXT("nebula-v2-%s.png"), View.Label), View.Location, View.Look, WithGas);
		const float InsideFactor = Gas->GetInsideFactor();
		GasVisible(false);
		Snap(*FString::Printf(TEXT("nebula-v2-%s-nogas.png"), View.Label), View.Location, View.Look, WithoutGas);
		GasVisible(true);
		const int32 Difference = Changed(WithGas, WithoutGas);
		AddInfo(FString::Printf(TEXT("nebula v2 %s: insideFactor=%.2f changedPixels=%d/%d meanLum gas=%.1f nogas=%.1f"), View.Label, InsideFactor, Difference, WithGas.Num(), MeanLuminance(WithGas), MeanLuminance(WithoutGas)));
		TestTrue(*FString::Printf(TEXT("%s view shows the gas"), View.Label), Difference > 2000);
	}

	// The close-range wisps on their own, from inside the cloud, looking at the one nearest the full-opacity band.
	if (Gas->Wisps)
	{
		FVector WispLook = Views[2].Look;
		double BestDistance = 1e12;
		for (int32 Instance = 0; Instance < Gas->Wisps->GetInstanceCount(); ++Instance)
		{
			FTransform Transform; Gas->Wisps->GetInstanceTransform(Instance, Transform, true);
			const FVector Relative = Transform.GetLocation()-Views[2].Location;
			if (FMath::Abs(Relative.Size()-900) < BestDistance) { BestDistance = FMath::Abs(Relative.Size()-900); WispLook = Relative.GetSafeNormal(); }
		}
		TArray<FColor> WispPixels;
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Capture->ShowOnlyComponents.Reset(); Capture->ShowOnlyComponent(Gas->Wisps);
		Snap(TEXT("nebula-v2-wisps-only.png"), Views[2].Location, WispLook, WispPixels);
		Capture->ShowOnlyComponents.Reset(); Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
		int32 WispVisible = 0;
		for (const FColor& Pixel : WispPixels) WispVisible += Pixel.R > 12 || Pixel.G > 12 || Pixel.B > 12;
		AddInfo(FString::Printf(TEXT("wisps alone cover %d of %d pixels"), WispVisible, WispPixels.Num()));
		TestTrue(TEXT("close-range wisps render inside the cloud"), WispVisible > 500);
	}

	// Lighting: the material's self-shadow, the sun's contribution and the emissive core, each switched off in turn.
	{
		auto SetScalar = [&](const TCHAR* Name, float Value) { for (const auto& Nebula : Gas->Nebulas) Nebula.Material->SetScalarParameterValue(Name, Value); };
		IConsoleVariable* DirectionalLights = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HeterogeneousVolumes.LightType.Directional"));
		TArray<FColor> Shadowed, Unshadowed, NoLights, NoEmission;
		Snap(TEXT("nebula-v2-exp-shadowed.png"), Views[0].Location, Views[0].Look, Shadowed);
		SetScalar(TEXT("ShadowStrength"), 0); Snap(TEXT("nebula-v2-exp-unshadowed.png"), Views[0].Location, Views[0].Look, Unshadowed);
		SetScalar(TEXT("ShadowStrength"), 1);
		DirectionalLights->Set(0, ECVF_SetByCode); Snap(TEXT("nebula-v2-exp-nolights.png"), Views[0].Location, Views[0].Look, NoLights);
		DirectionalLights->Set(1, ECVF_SetByCode);
		SetScalar(TEXT("BaseEmission"), 0); SetScalar(TEXT("CoreEmission"), 0);
		Snap(TEXT("nebula-v2-exp-noemission.png"), Views[0].Location, Views[0].Look, NoEmission);
		SetScalar(TEXT("BaseEmission"), .02f); SetScalar(TEXT("CoreEmission"), .15f);
		const int32 ShadowChanged = Changed(Shadowed, Unshadowed), LightChanged = Changed(Shadowed, NoLights), EmissionChanged = Changed(Shadowed, NoEmission);
		AddInfo(FString::Printf(TEXT("lighting: mean lum with shadow %.1f, without %.1f; sun off %.1f; emission off %.1f; changed pixels: shadow %d, sun %d, emission %d"),
			MeanLuminance(Shadowed), MeanLuminance(Unshadowed), MeanLuminance(NoLights), MeanLuminance(NoEmission), ShadowChanged, LightChanged, EmissionChanged));
		TestTrue(TEXT("the material self-shadow visibly changes the lit gas"), ShadowChanged > 1000);
		TestTrue(TEXT("the zone's directional lights visibly light the gas"), LightChanged > 1000);
	}

	// GPU cost is measured in PIE with stat gpu: a timed synchronous 4K capture measures wall time, not GPU time.
	return true;
}
#endif
