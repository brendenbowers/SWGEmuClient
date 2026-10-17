#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGSpaceTerrainReader.h"
#include "Common/SWGSpaceQuality.h"
#include "Components/SWGSpaceNebulaComponent.h"
#include "Components/SWGSpaceDustComponent.h"
#include "Components/SWGSpacePlanetComponent.h"
#include "Engine/PostProcessVolume.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Actor.h"

namespace
{
	// Space has no sun-lit surface to auto-expose on: a black sky would wash out under auto exposure.
	TAutoConsoleVariable<float> CVarSpaceExposureBias(
		TEXT("swg.Space.ExposureBias"), 0.0f,
		TEXT("Manual exposure (EV) of the space scene's post-process volume. Next zone load."));
	TAutoConsoleVariable<float> CVarSpaceBloomIntensity(
		TEXT("swg.Space.BloomIntensity"), 0.6f,
		TEXT("Bloom intensity of the space scene, so suns, engines and bright stars glow. Next zone load."));
	TAutoConsoleVariable<float> CVarSpaceBloomThreshold(
		TEXT("swg.Space.BloomThreshold"), 1.0f,
		TEXT("Bloom threshold of the space scene (scene-referred luminance). Next zone load."));
	TAutoConsoleVariable<float> CVarSpaceVignette(
		TEXT("swg.Space.Vignette"), 0.25f,
		TEXT("Vignette intensity of the space scene. Next zone load."));
	TAutoConsoleVariable<float> CVarSpaceLensFlare(
		TEXT("swg.Space.LensFlare"), 0.12f,
		TEXT("Lens flare intensity of the space scene (soft ghosts only, no streaks); 0 turns it off. Next zone load."));
	TAutoConsoleVariable<float> CVarSpaceLensFlareThreshold(
		TEXT("swg.Space.LensFlareThreshold"), 4.0f,
		TEXT("Scene-referred luminance above which a pixel feeds the lens flare; high, so only the sun disc (HDR 8) does. Next zone load."));
}

bool USWGTerrainSubsystem::SetupSpacePostProcess()
{
	APostProcessVolume* Volume = GetWorld()->SpawnActor<APostProcessVolume>();
	if (!Volume) return false;
	Volume->Tags.Add(TEXT("SWGPlanetLighting"));
	Volume->bUnbound = true;
	FPostProcessSettings& Settings = Volume->Settings;
	Settings.bOverride_AutoExposureMethod = true; Settings.AutoExposureMethod = AEM_Manual;
	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true; Settings.AutoExposureApplyPhysicalCameraExposure = false;
	Settings.bOverride_AutoExposureBias = true; Settings.AutoExposureBias = CVarSpaceExposureBias.GetValueOnGameThread();
	Settings.bOverride_BloomMethod = true; Settings.BloomMethod = BM_SOG;
	Settings.bOverride_BloomIntensity = true; Settings.BloomIntensity = CVarSpaceBloomIntensity.GetValueOnGameThread();
	Settings.bOverride_BloomThreshold = true; Settings.BloomThreshold = CVarSpaceBloomThreshold.GetValueOnGameThread();
	Settings.bOverride_VignetteIntensity = true; Settings.VignetteIntensity = CVarSpaceVignette.GetValueOnGameThread();
	Settings.bOverride_FilmGrainIntensity = true; Settings.FilmGrainIntensity = 0;
	Settings.bOverride_LensFlareIntensity = true; Settings.LensFlareIntensity = CVarSpaceLensFlare.GetValueOnGameThread();
	Settings.bOverride_LensFlareThreshold = true; Settings.LensFlareThreshold = CVarSpaceLensFlareThreshold.GetValueOnGameThread();
	Settings.bOverride_LensFlareBokehSize = true; Settings.LensFlareBokehSize = 1.5f;
	// Filmic tonemapper values are left at the engine defaults.
	return true;
}

bool USWGTerrainSubsystem::SetupSpaceLandmarks(const FSWGSpaceTerrainData& Data)
{
	FSWGSpaceQuality::Apply();
	SpaceNebula = NewObject<USWGSpaceNebulaComponent>(SkyActor);
	SpaceDust = NewObject<USWGSpaceDustComponent>(SkyActor);
	SpacePlanet = NewObject<USWGSpacePlanetComponent>(SkyActor);
	SpaceNebula->RegisterComponent(); SpaceDust->RegisterComponent(); SpacePlanet->RegisterComponent();
	if (!SpaceNebula->Setup(TreSubsystem, ActiveTerrainVirtualPath)) return false;
	if (!SpaceDust->Setup(Data, ActiveTerrainVirtualPath, TreSubsystem, MeshGenerator)) return false;
	TArray<ADirectionalLight*> SceneLights;
	for (ADirectionalLight* SceneLight : SpaceLights) SceneLights.Add(SceneLight);
	return SpacePlanet->Setup(Data, TreSubsystem, MeshGenerator,
		[this](const FString& ShaderName, bool bAdditive) { return CreateSkySprite(ShaderName, bAdditive); }, SceneLights, ActiveTerrainVirtualPath);
}

void USWGTerrainSubsystem::UpdateSpaceLandmarks(float DeltaTime, const FVector& Viewpoint)
{
	if (IsValid(SpaceNebula)) SpaceNebula->Update(Viewpoint, SpaceLights, DeltaTime);
	if (IsValid(SpacePlanet)) SpacePlanet->Update(DeltaTime, Viewpoint);
	if (IsValid(SpaceDust)) SpaceDust->Update(Viewpoint, SpaceLights, DeltaTime);
}
