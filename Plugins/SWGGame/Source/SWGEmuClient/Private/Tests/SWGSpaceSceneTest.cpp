#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SWGSpaceDustComponent.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Components/SWGSpaceNebulaComponent.h"
#include "Components/SWGSpacePlanetComponent.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/SWGAmbienceSubsystem.h"
#include "HAL/FileManager.h"
#include "Async/TaskGraphInterfaces.h"
#include "Components/SceneComponent.h"
#include "Subsystems/SWGWeatherSubsystem.h"
#include "Flow/States/SWGZoneLoadingState.h"
#include "Engine/Level.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "TRE/SWGDataTableReader.h"
#include "Common/SWGWorldScale.h"
#include "TRE/SWGNebulaReader.h"
#include "Common/SWGTerrainBake.h"
#include "Common/SWGPlanetGlobeBake.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceSceneTest, "SWG.Space.SceneLoading", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpaceSceneTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->InitializeStandalone();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) { Game->Shutdown(); return false; }
	Game->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(Game);
	ON_SCOPE_EXIT { Game->Shutdown(); World->DestroyWorld(false); };
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	USWGTerrainSubsystem* Terrain = Game->GetSubsystem<USWGTerrainSubsystem>();
	USWGWeatherSubsystem* Weather = Game->GetSubsystem<USWGWeatherSubsystem>();
	if (!Tre || !Terrain || !Weather) return false;
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives load"), Tre->LoadArchives())) return false;
	TestEqual(TEXT("STAT terrain selects the dedicated space level"), FSWGZoneLoadingState::GetLevelForTerrain(*Tre, TEXT("terrain/space_naboo.trn")), FName(TEXT("/Game/SWGEmu/Levels/SWGSpaceLevel")));
	TestEqual(TEXT("ground terrain selects the ground level"), FSWGZoneLoadingState::GetLevelForTerrain(*Tre, TEXT("terrain/rori.trn")), FName(TEXT("/Game/SWGEmu/Levels/SWGLevel")));
	UWorld* SpaceMap = LoadObject<UWorld>(nullptr, TEXT("/Game/SWGEmu/Levels/SWGSpaceLevel.SWGSpaceLevel"));
	if (TestNotNull(TEXT("dedicated space map is available"), SpaceMap) && SpaceMap->PersistentLevel)
	{
		for (AActor* Actor : SpaceMap->PersistentLevel->Actors)
		{
			if (!Actor) continue;
			TestFalse(TEXT("space map has no authored ground light or fog"), Actor->IsA<ADirectionalLight>() || Actor->IsA<AExponentialHeightFog>());
			TestNull(TEXT("space map has no ground atmosphere"), Actor->FindComponentByClass<USkyAtmosphereComponent>());
			TestNull(TEXT("space map has no ground volumetric clouds"), Actor->FindComponentByClass<UVolumetricCloudComponent>());
		}
		SpaceMap->DestroyWorld(false);
	}
	int32 Loaded = 0, Ready = 0;
	const FDelegateHandle LoadedHandle = Terrain->OnTerrainLoaded.AddLambda([&] { ++Loaded; });
	const FDelegateHandle ReadyHandle = Terrain->OnTerrainReady.AddLambda([&] { ++Ready; });
	// Pass 0 must bake Tatooine's region fresh and pass 1 must find it cached.
	TArray<FString> StaleCacheFiles;
	IFileManager::Get().FindFiles(StaleCacheFiles, *(USWGSpacePlanetComponent::GetRegionCacheDirectory()/TEXT("tatooine_*.*")), true, false);
	for (const FString& StaleFile : StaleCacheFiles) IFileManager::Get().Delete(*(USWGSpacePlanetComponent::GetRegionCacheDirectory()/StaleFile));
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		Terrain->BeginLoadTerrain(TEXT("terrain/space_tatooine.trn"), FVector::ZeroVector);
		TestTrue(TEXT("STAT scene is active"), Terrain->IsSpaceScene());
		TArray<FSWGEnvironmentRow> WeatherRows;
		if (TestTrue(TEXT("ground weather fixture loads"), USWGAmbienceSubsystem::LoadEnvironmentTable(*Tre, TEXT("rori"), WeatherRows)))
		{
			const FSWGEnvironmentRow* Clear = USWGAmbienceSubsystem::FindEnvironmentRow(WeatherRows, TEXT("global"), 0);
			if (TestNotNull(TEXT("ground clear weather fixture available"), Clear))
			{
				TestTrue(TEXT("fixture contains ground clouds"), !Clear->CloudBottomShader.IsEmpty() || !Clear->CloudTopShader.IsEmpty());
				for (float Alpha : {0.f, 0.5f, 1.f}) Terrain->BlendEnvironment(nullptr, *Clear, Alpha);
				Weather->StartBlend(*Clear, true);
				Weather->Tick(1.f);
				TestNull(TEXT("space tick discards ground weather state"), Weather->TargetRow);
			}
		}
		TestFalse(TEXT("no ground terrain data in space"), Terrain->GetPlanetData().IsValid());
		TestFalse(TEXT("no ground snapshot in space"), Terrain->GetSnapshotData().IsValid());
		TArray<UPrimitiveComponent*> Tiles;
		Terrain->GetTileComponents(Tiles);
		TestEqual(TEXT("no ground tiles in space"), Tiles.Num(), 0);
		int32 Faces = 0;
		int32 StarDomes = 0;
		int32 Planets = 0, Clouds = 0, Halos = 0, Atmospheres = 0, Celestials = 0;
		int32 DustFields = 0, NebulaCount = 0;
		bool bHasMainNebula = false;
		int32 GroundClouds = 0;
		UDynamicMeshComponent* MainSurface = nullptr;
		UDynamicMeshComponent* MainClouds = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->IsActorBeingDestroyed()) continue;
			TArray<UHeterogeneousVolumeComponent*> NebulaComponents; It->GetComponents(NebulaComponents);
			for (UHeterogeneousVolumeComponent* Nebula : NebulaComponents)
			{
				if (!Nebula->ComponentHasTag(TEXT("SWGSpaceNebula"))) continue;
				++NebulaCount;
				bHasMainNebula |= Nebula->GetComponentLocation().Equals(FVector(0,-400000,400000),1);
				TestEqual(TEXT("nebula has no walking collision"),Nebula->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
				TestEqual(TEXT("nebula uses native volume material"),Nebula->GetMaterial(0)->GetMaterial()->MaterialDomain,MD_Volume);
				TestNull(TEXT("nebula is independent of camera-centred sky actor"),Nebula->GetAttachParent());
				const int32 Quality = FMath::Clamp(IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.NebulaQuality"))->GetInt(),0,3);
				// Resolution is a quality-tier value (halved for far volumes), never an SVT-driven one.
				TestTrue(TEXT("nebula resolution is its quality tier near or far value"),Nebula->VolumeResolution == USWGSpaceNebulaComponent::GetVolumeResolution(Quality,false)
					|| Nebula->VolumeResolution == USWGSpaceNebulaComponent::GetVolumeResolution(Quality,true));
			}
			TArray<UInstancedStaticMeshComponent*> DustComponents; It->GetComponents(DustComponents);
			for (UInstancedStaticMeshComponent* Dust : DustComponents)
			{
				if (Dust->GetFName() != FName(TEXT("SWGSpaceDust"))) continue;
				++DustFields;
				TestEqual(TEXT("retail dust count"), Dust->GetInstanceCount(), 750);
				TestEqual(TEXT("dust material is the streak quad material"), Dust->GetMaterial(0)->GetMaterial()->GetName(), FString(TEXT("M_SWGSpaceDust")));
				TestTrue(TEXT("stationary dust has no flight streak"), It->FindComponentByClass<USWGSpaceDustComponent>()->GetStreakVector().IsNearlyZero());
				TestFalse(TEXT("zone asteroid fields are read for debris"), It->FindComponentByClass<USWGSpaceDustComponent>()->Fields.IsEmpty());
				{
					const USWGSpaceDustComponent* DustComponent = It->FindComponentByClass<USWGSpaceDustComponent>();
					int32 ExpectedBudget = 0;
					for (const FSWGAsteroidFieldData& Field : DustComponent->Fields) ExpectedBudget = FMath::Max(ExpectedBudget, FMath::Clamp(Field.NumRadialAsteroids, 0, USWGSpaceDustComponent::MaxDebrisCount));
					TestEqual(TEXT("Tatooine's largest radial asteroid count is 74"), ExpectedBudget, 74);
					TestEqual(TEXT("one style per field"), DustComponent->FieldStyles.Num(), DustComponent->Fields.Num());
					TestEqual(TEXT("debris pieces are the largest field budget exactly when the debris cvar is on"), DustComponent->Debris.Num(), IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.Debris"))->GetBool() ? ExpectedBudget : 0);
					if (DustComponent->FieldStyles.IsValidIndex(0))
					{
						TestEqual(TEXT("field 0 radial style: 50 rocks"), DustComponent->FieldStyles[0].Count, 50);
						TestEqual(TEXT("field 0 spawn range is the table's 100 m"), DustComponent->FieldStyles[0].SpawnRange, 100.0*SWGWorldScale);
						TestEqual(TEXT("field 0 delete range is the table's 100 m"), DustComponent->FieldStyles[0].DeleteRange, 100.0*SWGWorldScale);
						TestEqual(TEXT("field 0 scale min"), DustComponent->FieldStyles[0].ScaleMin, .5f);
						TestEqual(TEXT("field 0 scale max"), DustComponent->FieldStyles[0].ScaleMax, 1.f);
					}
				}
				for (int32 Instance = 0; Instance < Dust->GetInstanceCount(); ++Instance)
				{
					FTransform Transform; Dust->GetInstanceTransform(Instance, Transform, true);
					TestTrue(TEXT("dust is bounded by the scene radius"), Transform.GetLocation().Size() <= 6400.01);
					TestTrue(TEXT("dust quad is shown or hull-excluded"), Transform.GetScale3D().Equals(FVector::OneVector) || Transform.GetScale3D().IsNearlyZero());
				}
			}
			TArray<UDynamicMeshComponent*> Components;
			It->GetComponents(Components);
			for (UDynamicMeshComponent* Component : Components)
			{
				if (Component->ComponentHasTag(TEXT("SWGSpacePlanetSurface")) || Component->ComponentHasTag(TEXT("SWGSpacePlanetCloud")))
				{
					const double Distance = FVector::Dist(Component->GetComponentLocation(), It->GetActorLocation());
					// World bounds of a rotated sphere overstate its radius; measure the mesh itself.
					const double Radius = Component->GetMesh()->GetBounds().Extents().GetMax()*Component->GetComponentScale().GetMax();
					TestTrue(TEXT("planets sit behind everything in the 8 km zone"), Distance-Radius >= USWGSpacePlanetComponent::PlanetNearestDistance*.99);
					TestTrue(TEXT("stars stay behind the planets"), Distance+Radius < USWGSpacePlanetComponent::StarDomeRadius);
					TestTrue(TEXT("ship lights and shadows never reach planets"), !Component->LightingChannels.bChannel0 && Component->LightingChannels.bChannel1);
				}
				if (Component->ComponentHasTag(TEXT("SWGSpacePlanetSurface")))
				{
					++Planets; if (!MainSurface) MainSurface = Component;
					const UE::Geometry::FDynamicMesh3* Mesh = Component->GetMesh();
					if (TestNotNull(TEXT("planet mesh available"), Mesh) && Mesh->TriangleCount())
					{
						const int32 Triangle = *Mesh->TriangleIndicesItr().begin();
						TestTrue(TEXT("planet triangles face outward with Unreal sphere winding"),
							Mesh->GetTriNormal(Triangle).Dot(Mesh->GetTriCentroid(Triangle)) > 0);
					}
				}
				else if (Component->ComponentHasTag(TEXT("SWGSpacePlanetCloud"))) { ++Clouds; MainClouds = Component; }
				else if (Component->ComponentHasTag(TEXT("SWGSpacePlanetAtmosphere")))
				{
					++Atmospheres;
					TestEqual(TEXT("atmosphere has no walking collision"), Component->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
					TestFalse(TEXT("atmosphere respects interior occlusion"), Component->GetMaterial(0)->GetMaterial()->bDisableDepthTest);
					TestTrue(TEXT("atmosphere shell is on the planet lighting channel only"), !Component->LightingChannels.bChannel0 && Component->LightingChannels.bChannel1);
					TestEqual(TEXT("atmosphere uses the sun-aware v2 material"), Component->GetMaterial(0)->GetMaterial()->GetName(), FString(TEXT("M_SWGPlanetAtmosphere")));
				}
				else ++Faces;
			}
			TArray<UStaticMeshComponent*> StaticComponents; It->GetComponents(StaticComponents);
			for (UStaticMeshComponent* Component : StaticComponents)
			{
				UMaterialInterface* Applied = Component->GetMaterial(0);
				GroundClouds += Applied && Applied->GetMaterial()->GetName() == TEXT("M_SWGCloudLayer");
				Halos += Component->ComponentHasTag(TEXT("SWGSpacePlanetHalo"));
				Celestials += Component->ComponentHasTag(TEXT("SWGSpaceCelestial"));
				if (Component->ComponentHasTag(TEXT("SWGSpacePlanetHalo")) || Component->ComponentHasTag(TEXT("SWGSpaceCelestial")))
				{
					UMaterialInterface* Material = Component->GetMaterial(0);
					if (TestNotNull(TEXT("landmark sprite material available"), Material))
						TestFalse(TEXT("landmark sprites respect interior occlusion"), Material->GetMaterial()->bDisableDepthTest);
				}
				if (Component->ComponentHasTag(TEXT("SWGSpaceCelestial")))
					TestTrue(TEXT("suns sit behind the star dome"), FVector::Dist(Component->GetComponentLocation(), It->GetActorLocation()) > USWGSpacePlanetComponent::StarDomeRadius);
				if (Component->GetFName() != FName(TEXT("SWGStarDome"))) continue;
				++StarDomes;
				TestTrue(TEXT("star dome sits at the background star radius"), FMath::IsNearlyEqual(Component->Bounds.BoxExtent.GetMax(),USWGSpacePlanetComponent::StarDomeRadius, USWGSpacePlanetComponent::StarDomeRadius*.01));
				UMaterialInstanceDynamic* Stars = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0));
				if (!TestNotNull(TEXT("space star material created"), Stars)) continue;
				float Intensity = 0;
				Stars->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Intensity")), Intensity);
				TestTrue(TEXT("space stars remain visible without ground day/night fade"), Intensity > 0);
				UTexture* Texture = nullptr;
				Stars->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Stars")), Texture);
				UTexture2D* Map = Cast<UTexture2D>(Texture);
				if (!TestNotNull(TEXT("star map supplied"), Map)) continue;
				TestEqual(TEXT("star map width"), Map->GetSizeX(), 8192);
				TestEqual(TEXT("star map height"), Map->GetSizeY(), 4096);
				const FColor* Pixels = static_cast<const FColor*>(Map->GetPlatformData()->Mips[0].BulkData.LockReadOnly());
				int32 Points = 0;
				for (int32 Pixel = 0; Pixel < Map->GetSizeX()*Map->GetSizeY(); ++Pixel)
					Points += Pixels[Pixel].R != 0 || Pixels[Pixel].G != 0 || Pixels[Pixel].B != 0;
				Map->GetPlatformData()->Mips[0].BulkData.Unlock();
				TestTrue(TEXT("space star density uses 24000 entries instead of the old 3500 default"), Points > 20000);
			}
		}
		TestEqual(TEXT("six sky faces, including after same-zone reload"), Faces, 6);
		TestEqual(TEXT("ground weather cannot create cloud sheets in space"), GroundClouds, 0);
		TestEqual(TEXT("one star dome, including after same-zone reload"), StarDomes, 1);
		TestEqual(TEXT("four planet surfaces, including after reload"), Planets, 4);
		TestEqual(TEXT("one planetary cloud layer"), Clouds, 1);
		TestEqual(TEXT("three independent moon and distant planet halos"), Halos, 3);
		TestEqual(TEXT("one atmosphere shell per planet, including after reload"), Atmospheres, Planets);
		TestEqual(TEXT("sixteen celestial sprites"), Celestials, 16);
		TestEqual(TEXT("one dust field, including after reload"), DustFields, 1);
		FSWGDataTableData NebulaTable; TArray<FSWGNebulaData> Nebulas;
		TestTrue(TEXT("authored nebula table available for comparison"),FSWGDataTableReader::ReadDataTable(
			Tre->CreateIffReader(TEXT("datatables/space/nebula/space_tatooine.iff")),NebulaTable) && FSWGNebulaReader::Read(NebulaTable,Nebulas));
		TestEqual(TEXT("one native volume per positive-density entry, including reload"),NebulaCount,
			Nebulas.FilterByPredicate([](const FSWGNebulaData& Nebula) { return Nebula.Density > 0; }).Num());
		TestTrue(TEXT("main nebula uses authored space position, not camera-relative position"),bHasMainNebula);
		AddInfo(FString::Printf(TEXT("native gas volumes=%d main position verified=%d reload=%d"),NebulaCount,bHasMainNebula,Pass));
		if (MainClouds && MainClouds->GetAttachParent())
		{
			MainSurface = nullptr;
			for (USceneComponent* Child : MainClouds->GetAttachParent()->GetAttachChildren())
				if (Child->ComponentHasTag(TEXT("SWGSpacePlanetSurface"))) MainSurface = Cast<UDynamicMeshComponent>(Child);
			TestNotNull(TEXT("clouds share a planet frame with their surface"), MainSurface);
		}
		if (MainSurface && MainClouds)
		{
			const float SurfaceYaw = MainSurface->GetRelativeRotation().Yaw, CloudYaw = MainClouds->GetRelativeRotation().Yaw;
			Terrain->Tick(2.f);
			TestTrue(TEXT("surface uses PLNT spin rate"), FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(SurfaceYaw, float(MainSurface->GetRelativeRotation().Yaw)), 0.01f, 0.001f));
			TestTrue(TEXT("clouds rotate independently at PLNT cloud rate"), FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(CloudYaw, float(MainClouds->GetRelativeRotation().Yaw)), 0.3f, 0.001f));
		}
		USWGSpacePlanetComponent* PlanetField = Terrain->SpacePlanet;
		if (TestNotNull(TEXT("planet component exists"), PlanetField) && TestFalse(TEXT("space has lights for the sun"), Terrain->SpaceLights.IsEmpty()))
		{
			TestEqual(TEXT("every planet visual owns an atmosphere"), PlanetField->Planets.Num(), Atmospheres);
			ADirectionalLight* KeyLight = Terrain->SpaceLights[0];
			auto CheckSunMatchesLight = [&](const TCHAR* Context)
			{
				const FVector TowardLight = -KeyLight->GetActorForwardVector();
				TestTrue(*FString::Printf(TEXT("planet sun direction follows the key light %s"), Context), PlanetField->HasSun() && PlanetField->GetSunDirection().Equals(TowardLight, 1e-3));
				for (const USWGSpacePlanetComponent::FPlanetVisual& Planet : PlanetField->Planets)
				{
					FLinearColor Sun = FLinearColor::Black;
					if (!TestNotNull(TEXT("planet atmosphere material"), Planet.AtmosphereMaterial.Get())) continue;
					Planet.AtmosphereMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("SunDirection")), Sun);
					TestTrue(*FString::Printf(TEXT("atmosphere SunDirection follows the light %s"), Context), FVector(Sun.R, Sun.G, Sun.B).Equals(TowardLight, 1e-3));
					if (Planet.SurfaceMaterial)
					{
						Planet.SurfaceMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("SunDirection")), Sun);
						TestTrue(*FString::Printf(TEXT("surface SunDirection follows the light %s"), Context), FVector(Sun.R, Sun.G, Sun.B).Equals(TowardLight, 1e-3));
					}
				}
			};
			CheckSunMatchesLight(TEXT("as loaded"));
			for (const USWGSpacePlanetComponent::FPlanetVisual& Planet : PlanetField->Planets)
			{
				if (!TestNotNull(TEXT("planet atmosphere shell"), Planet.Atmosphere.Get())) continue;
				TestTrue(TEXT("every planet's shell is lit on lighting channel 1 only"), !Planet.Atmosphere->LightingChannels.bChannel0 && Planet.Atmosphere->LightingChannels.bChannel1);
			}
			// Turn the key light away from the scene's own direction; the next planet update must follow it.
			const FRotator OriginalRotation = KeyLight->GetActorRotation();
			KeyLight->SetActorRotation(OriginalRotation + FRotator(40, 110, 0));
			PlanetField->Update(0, FVector::ZeroVector);
			CheckSunMatchesLight(TEXT("after the light turned"));
			KeyLight->SetActorRotation(OriginalRotation);
			PlanetField->Update(0, FVector::ZeroVector);
		}
		if (PlanetField)
		{
			// The region bakes on a worker and lands on the game thread; wait for it.
			const double RegionWaitStart = FPlatformTime::Seconds();
			while (PlanetField->GetRegion().bPending && FPlatformTime::Seconds() - RegionWaitStart < 300)
			{
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread); FPlatformProcess::Sleep(.05f);
			}
			const USWGSpacePlanetComponent::FRegionState& Region = PlanetField->GetRegion();
			TestEqual(TEXT("the zone's ground terrain is Tatooine's"), Region.GroundTerrain, FString(TEXT("terrain/tatooine.trn")));
			if (TestTrue(TEXT("the primary planet has a region, ready after the bake"), Region.bReady && PlanetField->Planets.IsValidIndex(Region.PlanetIndex)))
			{
				const USWGSpacePlanetComponent::FPlanetVisual& Primary = PlanetField->Planets[Region.PlanetIndex];
				float Enabled = 0, CapHalfWidth = 0; UTexture* ColourTexture = nullptr; UTexture* NormalTexture = nullptr; UTexture* FarTexture = nullptr; FLinearColor Centre = FLinearColor::Black;
				Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("RegionEnabled")), Enabled);
				Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("RegionCapHalfWidth")), CapHalfWidth);
				Primary.SurfaceMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("RegionColor")), ColourTexture);
				Primary.SurfaceMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("RegionNormal")), NormalTexture);
				Primary.SurfaceMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("GlobeFar")), FarTexture);
				Primary.SurfaceMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("RegionCentre")), Centre);
				TestEqual(TEXT("Tatooine's surface MID has RegionEnabled = 1"), Enabled, 1.f);
				const int32 ExpectedCapResolution = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.PlanetRegionResolution"))->GetInt();
				TestEqual(TEXT("the cap map follows swg.Space.PlanetRegionResolution"), Region.CapResolution, ExpectedCapResolution);
				TestTrue(TEXT("the globe cap colour is applied as RegionColor"), ColourTexture && ColourTexture == PlanetField->GetRegionColourTexture() && ColourTexture->GetSurfaceWidth() == ExpectedCapResolution);
				TestTrue(TEXT("the globe cap relief is applied as RegionNormal"), NormalTexture && NormalTexture == PlanetField->GetRegionNormalTexture() && NormalTexture->GetSurfaceWidth() == ExpectedCapResolution);
				FSWGPlanetGlobeParams ExpectedLayout; ExpectedLayout.CapResolution = ExpectedCapResolution;
				TestTrue(TEXT("the whole-globe map is applied as GlobeFar, twice as wide as tall (4096 x 2048 at most)"), FarTexture && FarTexture == PlanetField->GetGlobeFarTexture()
					&& FarTexture->GetSurfaceWidth() == ExpectedLayout.GetFarWidth() && FarTexture->GetSurfaceHeight() == ExpectedLayout.GetFarHeight());
				// Stage 10: the ground bake follows the cap map's resolution at the top tier, and the detail families get their retail textures.
				const int32 ExpectedGroundResolution = ExpectedCapResolution > 2048 ? FMath::Min(ExpectedCapResolution, FSWGTerrainBake::MaxResolution) : FMath::Min(ExpectedCapResolution, FSWGTerrainBake::DefaultResolution);
				TestEqual(TEXT("the ground bake resolution follows the quality tier"), Region.GroundResolution, ExpectedGroundResolution);
				UTexture* DetailWeightsTexture = nullptr; UTexture* FamilyTexture = nullptr; float DetailStrength = -1, DetailRepeat = -1, LandRoughness = -1, LandSpecular = -1, WaterRoughness = -1, WaterSpecular = -1;
				Primary.SurfaceMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("RegionDetail")), DetailWeightsTexture);
				Primary.SurfaceMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("FamilyTex0")), FamilyTexture);
				PlanetField->Update(0, Primary.Surface->GetOwner()->GetActorLocation());
				Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DetailStrength")), DetailStrength);
				Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DetailRepeat")), DetailRepeat);
				Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("LandRoughness")), LandRoughness);
				Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("LandSpecular")), LandSpecular);
				Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("WaterRoughness")), WaterRoughness);
				Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("WaterSpecular")), WaterSpecular);
				TestTrue(TEXT("the detail weight map is applied as RegionDetail at half the cap map's resolution"), DetailWeightsTexture && DetailWeightsTexture == PlanetField->GetRegionDetailTexture()
					&& DetailWeightsTexture->GetSurfaceWidth() == ExpectedLayout.GetCapDetailResolution());
				TestTrue(TEXT("Tatooine's ground has detail families with retail textures"), Region.DetailFamilyCount >= 1 && FamilyTexture != nullptr);
				TestTrue(TEXT("the detail is on at its cvar strength and tiled from the ground scale"), DetailStrength > 0.f && DetailRepeat > 1.f && Region.GroundPerRadian > 1000.f);
				TestTrue(TEXT("land is fully rough and non-specular (no plastic sheen)"), LandRoughness >= .95f && LandSpecular <= .1f);
				TestTrue(TEXT("water keeps a smooth, specular glint"), WaterRoughness <= .25f && WaterSpecular >= .3f);
				// The sun and stars: the cross-texture sprites are redrawn by the code-built material (needs M_SWGSunSprite from swg.CreateSpacePlanetAssets).
				int32 StarSprites = 0;
				for (const USWGSpacePlanetComponent::FCelestialVisual& Celestial : PlanetField->Celestials) StarSprites += Celestial.StarMaterial != nullptr;
				TestTrue(TEXT("the cross-texture celestial sprites use the code-built sun material"), StarSprites > 0 && PlanetField->GetMainSunIndex() != INDEX_NONE && PlanetField->Celestials[PlanetField->GetMainSunIndex()].bMainSun);
				// The cap radius is the geometry of the scene: acos(surface radius / distance from the viewer), and the cap map spans 1.25 of it.
				const float SurfaceRadius = float(Primary.Surface->GetMesh()->GetBounds().MaxDim()*.5*Primary.Surface->GetComponentScale().X);
				const float ViewerDistance = float((Primary.Surface->GetOwner()->GetActorLocation() - Primary.Surface->GetComponentLocation()).Size());
				TestTrue(*FString::Printf(TEXT("the cap radius %.1f deg is acos(radius / distance) = %.1f deg"), FMath::RadiansToDegrees(Region.CapRadius),
					FMath::RadiansToDegrees(USWGSpacePlanetComponent::ComputeCapRadius(SurfaceRadius, ViewerDistance))), FMath::IsNearlyEqual(Region.CapRadius, USWGSpacePlanetComponent::ComputeCapRadius(SurfaceRadius, ViewerDistance), .02f));
				TestTrue(TEXT("Tatooine's visible cap is the authored 40 to 50 degrees across"), Region.CapRadius > FMath::DegreesToRadians(18.f) && Region.CapRadius < FMath::DegreesToRadians(30.f));
				TestTrue(TEXT("the cap map half width is 1.25 cap radii"), FMath::IsNearlyEqual(CapHalfWidth, Region.CapRadius*1.25f, 1e-4f) && FMath::IsNearlyEqual(CapHalfWidth, Region.CapHalfWidth, 1e-4f));
				TestEqual(TEXT("the fill follows swg.Space.PlanetRegionFill"), Region.Fill, IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.PlanetRegionFill"))->GetFloat());
				TestNull(TEXT("swg.Space.PlanetRegionSize is retired"), IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.PlanetRegionSize")));
				TestEqual(TEXT("the primary planet's surface stops spinning so its terrain keeps facing the viewer"), Primary.SurfaceSpin, 0.f);
				TestTrue(TEXT("the region centre is a unit vector"), FMath::IsNearlyEqual(FVector(Centre.R, Centre.G, Centre.B).Size(), 1.0, 1e-3));
				// The centre faces the viewer: object space turned back to the world points from the planet to the sky actor.
				const FVector CentreWorld = Primary.Surface->GetComponentTransform().TransformVectorNoScale(FVector(Centre.R, Centre.G, Centre.B));
				const FVector ToViewer = (Primary.Surface->GetOwner()->GetActorLocation() - Primary.Surface->GetComponentLocation()).GetSafeNormal();
				TestTrue(TEXT("the region faces the viewer"), CentreWorld.Equals(ToViewer, 1e-3));
				// The clouds over the primary planet thin where the viewer looks straight down (live from the cvar).
				PlanetField->Update(0, Primary.Surface->GetOwner()->GetActorLocation());
				if (TestNotNull(TEXT("the primary planet has a cloud layer"), Primary.CloudMaterial.Get()))
				{
					float ThinStrength = -1, ThinRadius = -1; FLinearColor ThinDirection = FLinearColor::Black;
					Primary.CloudMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ThinStrength")), ThinStrength);
					Primary.CloudMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ThinRadius")), ThinRadius);
					Primary.CloudMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("ThinDirection")), ThinDirection);
					TestEqual(TEXT("cloud thinning follows swg.Space.PlanetCloudThinning"), ThinStrength, IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.PlanetCloudThinning"))->GetFloat());
					TestTrue(TEXT("cloud thinning spans the visible cap"), FMath::IsNearlyEqual(ThinRadius, Region.CapRadius, 1e-4f));
					TestTrue(TEXT("cloud thinning looks toward the viewer"), FVector(ThinDirection.R, ThinDirection.G, ThinDirection.B).Equals(ToViewer, 1e-3));
				}
				if (Pass == 0) TestFalse(TEXT("the first load bakes the region and the globe"), Region.bFromCache || Region.bGlobeFromCache);
				else TestTrue(TEXT("the second load hits the cache for the ground and the globe"), Region.bFromCache && Region.bGlobeFromCache);
				TestTrue(TEXT("the ground cache files exist"), IFileManager::Get().FileExists(*(Region.CacheBasePath + TEXT(".png"))) && IFileManager::Get().FileExists(*(Region.CacheBasePath + TEXT(".bin"))));
				TestTrue(TEXT("the globe cache files exist"), IFileManager::Get().FileExists(*(Region.GlobeCacheBasePath + TEXT(".png"))) && IFileManager::Get().FileExists(*(Region.GlobeCacheBasePath + TEXT("_far.png")))
					&& IFileManager::Get().FileExists(*(Region.GlobeCacheBasePath + TEXT(".bin"))));
				AddInfo(FString::Printf(TEXT("Tatooine globe pass %d: ground %s (bake %.2f s), globe %s (build %.2f s), worker %.2f s, cap %.1f deg, cap map %d"), Pass, Region.bFromCache ? TEXT("cache hit") : TEXT("fresh bake"),
					Region.BakeSeconds, Region.bGlobeFromCache ? TEXT("cache hit") : TEXT("fresh build"), Region.GlobeSeconds, Region.LoadSeconds, FMath::RadiansToDegrees(Region.CapRadius), Region.CapResolution));
				for (int32 PlanetIndex = 0; PlanetIndex < PlanetField->Planets.Num(); ++PlanetIndex)
				{
					if (PlanetIndex == Region.PlanetIndex || !PlanetField->Planets[PlanetIndex].SurfaceMaterial) continue;
					float OtherEnabled = 1; UTexture* OtherTexture = nullptr;
					PlanetField->Planets[PlanetIndex].SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("RegionEnabled")), OtherEnabled);
					PlanetField->Planets[PlanetIndex].SurfaceMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("RegionColor")), OtherTexture);
					TestEqual(TEXT("every other planet and moon keeps RegionEnabled = 0"), OtherEnabled, 0.f);
					TestTrue(TEXT("other planets have no region texture"), OtherTexture == nullptr || OtherTexture != ColourTexture);
				}
			}
		}
		TestEqual(TEXT("scene loaded exactly once per load"), Loaded, Pass + 1);
		TestEqual(TEXT("space readiness exactly once per load"), Ready, Pass + 1);
	}
	// Every zone's primary planet takes its ground region through M_SWGPlanetSurface, including single-texture shaders; moons and other planets keep RegionEnabled = 0.
	for (const TCHAR* Zone : {TEXT("naboo"), TEXT("corellia")})
	{
		const FString Label(Zone);
		Terrain->BeginLoadTerrain(FString::Printf(TEXT("terrain/space_%s.trn"), Zone), FVector::ZeroVector);
		USWGSpacePlanetComponent* ZonePlanets = Terrain->SpacePlanet;
		if (!TestTrue(*(Label + TEXT(" loads a space scene with planets")), Terrain->IsSpaceScene() && ZonePlanets && !ZonePlanets->Planets.IsEmpty())) continue;
		const double WaitStart = FPlatformTime::Seconds();
		while (ZonePlanets->GetRegion().bPending && FPlatformTime::Seconds() - WaitStart < 300)
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread); FPlatformProcess::Sleep(.05f);
		}
		const USWGSpacePlanetComponent::FRegionState& ZoneRegion = ZonePlanets->GetRegion();
		TestEqual(*(Label + TEXT("'s ground terrain is its own")), ZoneRegion.GroundTerrain, FString::Printf(TEXT("terrain/%s.trn"), Zone));
		if (!TestTrue(*(Label + TEXT(" primary planet has a ready region")), ZoneRegion.bReady && ZonePlanets->Planets.IsValidIndex(ZoneRegion.PlanetIndex))) continue;
		for (int32 PlanetIndex = 0; PlanetIndex < ZonePlanets->Planets.Num(); ++PlanetIndex)
		{
			const USWGSpacePlanetComponent::FPlanetVisual& Visual = ZonePlanets->Planets[PlanetIndex];
			if (!TestNotNull(*FString::Printf(TEXT("%s planet %d surface goes through the planet surface material"), Zone, PlanetIndex), Visual.SurfaceMaterial.Get())) continue;
			TestEqual(*FString::Printf(TEXT("%s planet %d uses M_SWGPlanetSurface"), Zone, PlanetIndex), Visual.SurfaceMaterial->GetMaterial()->GetName(), FString(TEXT("M_SWGPlanetSurface")));
			float Enabled = -1; UTexture* DetailA = nullptr; UTexture* DetailB = nullptr;
			Visual.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("RegionEnabled")), Enabled);
			Visual.SurfaceMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("DetailA")), DetailA);
			Visual.SurfaceMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("DetailB")), DetailB);
			TestEqual(*FString::Printf(TEXT("%s planet %d RegionEnabled follows being the primary"), Zone, PlanetIndex), Enabled, PlanetIndex == ZoneRegion.PlanetIndex ? 1.f : 0.f);
			TestTrue(*FString::Printf(TEXT("%s planet %d has its main texture in both detail slots when single-texture"), Zone, PlanetIndex), DetailA != nullptr && DetailB != nullptr);
		}
		AddInfo(FString::Printf(TEXT("%s primary planet %d globe %s, cap radius %.1f deg"), Zone, ZoneRegion.PlanetIndex, ZoneRegion.bFromCache ? TEXT("from cache") : TEXT("freshly baked"), FMath::RadiansToDegrees(ZoneRegion.CapRadius)));
	}
	Terrain->OnTerrainLoaded.Remove(LoadedHandle); Terrain->OnTerrainReady.Remove(ReadyHandle);
	return true;
}
#endif
