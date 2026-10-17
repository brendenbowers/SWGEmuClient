#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Async/TaskGraphInterfaces.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Engine/DirectionalLight.h"
#include "Components/DynamicMeshComponent.h"
#include "MaterialShared.h"
#include "SceneInterface.h"
#include "ImageUtils.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif
#include "Common/SWGWorldScale.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Components/SWGSpaceNebulaComponent.h"
#include "Components/SWGSpaceDustComponent.h"
#include "Components/SWGSpacePlanetComponent.h"
#include "TRE/SWGNebulaReader.h"
#include "TRE/SWGAsteroidFieldReader.h"
#include "TRE/SWGSpaceTerrainReader.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	constexpr int32 CaptureWidth = 1280, CaptureHeight = 720;
	constexpr float FrameSeconds = 1.f/60.f;
}

/** A space-only world: no ship, a plain actor stands in for the pilot so dust can see it move. */
struct FSWGSpaceZoneFixture
	{
		UGameInstance* Game = nullptr;
		UWorld* World = nullptr;
		USWGTreSubsystem* Tre = nullptr;
		USWGMeshGeneratorSubsystem* Meshes = nullptr;
		USWGObjectGraphSubsystem* Graph = nullptr;
		USWGTerrainSubsystem* Terrain = nullptr;
		AActor* Pilot = nullptr;
		USceneCaptureComponent2D* Capture = nullptr;
		UTextureRenderTarget2D* Target = nullptr;
		FString LastError;

		USWGSpacePlanetComponent* Planet() const { return Terrain->SpacePlanet; }
		USWGSpaceNebulaComponent* Nebula() const { return Terrain->SpaceNebula; }
		USWGSpaceDustComponent* Dust() const { return Terrain->SpaceDust; }
		const TArray<TObjectPtr<ADirectionalLight>>& Lights() const { return Terrain->SpaceLights; }

		bool Init(FAutomationTestBase& Test)
		{
			Game = NewObject<UGameInstance>(GEngine);
			Game->AddToRoot(); Game->InitializeStandalone();
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World) return false;
			World->AddToRoot(); Game->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(Game);
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay();
			Tre = Game->GetSubsystem<USWGTreSubsystem>(); Meshes = Game->GetSubsystem<USWGMeshGeneratorSubsystem>();
			Graph = Game->GetSubsystem<USWGObjectGraphSubsystem>(); Terrain = Game->GetSubsystem<USWGTerrainSubsystem>();
			if (!Tre || !Meshes || !Graph || !Terrain) return false;
			Tre->LoadConfig();
			if (!Test.TestTrue(TEXT("retail archives available"), Tre->LoadArchives())) return false;
			Terrain->OnTerrainError.AddLambda([this](const FString& Message) { LastError = Message; });
			Pilot = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Pilot);
			Pilot->SetRootComponent(Root); Root->RegisterComponent();
			Graph->RegisterStaticObject(1, Pilot); Graph->LocalPlayerObjectId = 1; Graph->bSpaceScene = true;
			AActor* CaptureActor = World->SpawnActor<AActor>();
			Capture = NewObject<USceneCaptureComponent2D>(CaptureActor);
			CaptureActor->SetRootComponent(Capture); Capture->RegisterComponent();
			Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false;
			Capture->ShowFlags.SetHeterogeneousVolumes(true); Capture->ShowFlags.SetAntiAliasing(false);
			Capture->ShowFlags.SetMotionBlur(false); Capture->ShowFlags.SetGrain(false); Capture->ShowFlags.SetSeparateTranslucency(true);
			Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			Target = NewObject<UTextureRenderTarget2D>(CaptureActor);
			Target->InitCustomFormat(CaptureWidth, CaptureHeight, PF_B8G8R8A8, false); Capture->TextureTarget = Target;
			return true;
		}

		void Shutdown()
		{
			if (Game) Game->Shutdown();
			if (World) { World->DestroyWorld(false); World->RemoveFromRoot(); }
			if (Game) Game->RemoveFromRoot();
		}

		void Pump(int32 Frames)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Meshes->Tick(FrameSeconds); World->Tick(LEVELTICK_All, FrameSeconds);
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			}
		}

		/** Loads the zone and waits for its region bake and asynchronous meshes; false if the scene did not come up. */
		bool LoadZone(const FString& Zone)
		{
			Pilot->SetActorLocation(FVector::ZeroVector);
			Terrain->BeginLoadTerrain(FString::Printf(TEXT("terrain/space_%s.trn"), *Zone), FVector::ZeroVector);
			if (!Terrain->IsSpaceScene() || !Terrain->SpacePlanet || !Terrain->SpaceNebula || !Terrain->SpaceDust)
			{
				UE_LOG(LogTemp, Warning, TEXT("space zone %s did not come up: scene=%d planet=%d nebula=%d dust=%d error=%s"), *Zone, Terrain->IsSpaceScene(), Terrain->SpacePlanet != nullptr, Terrain->SpaceNebula != nullptr, Terrain->SpaceDust != nullptr, *LastError);
				return false;
			}
			const double Start = FPlatformTime::Seconds();
			while (Terrain->SpacePlanet->GetRegion().bPending && FPlatformTime::Seconds()-Start < 300) { Pump(1); FPlatformProcess::Sleep(.02f); }
			Pump(30);
			Terrain->Tick(FrameSeconds);
			PrepareShaders();
			Pump(5);
			return true;
		}

		/** Compiles only the shaders still missing for the capture platform; recaching a complete shader map blacks out later renders. */
		void PrepareShaders()
		{
#if WITH_EDITOR
			TSet<FMaterialResource*> Prepared;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				TArray<UPrimitiveComponent*> Components; It->GetComponents(Components);
				for (UPrimitiveComponent* Component : Components)
				{
					TArray<UMaterialInterface*> Materials; Component->GetUsedMaterials(Materials);
					for (UMaterialInterface* Material : Materials)
					{
						FMaterialResource* Resource = Material ? Material->GetMaterialResource(World->Scene->GetShaderPlatform()) : nullptr;
						if (Resource && !Prepared.Contains(Resource) && !Resource->IsGameThreadShaderMapComplete())
						{
							Resource->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
							Prepared.Add(Resource);
						}
					}
				}
			}
			FAssetCompilingManager::Get().FinishAllCompilation();
#endif
		}

		/** Moves the capture and the sky actor to Location, as the terrain subsystem does from the player camera. */
		void PlaceCamera(const FVector& Location, const FRotator& Rotation, float FieldOfView)
		{
			Capture->SetWorldLocation(Location); Capture->SetWorldRotation(Rotation); Capture->FOVAngle = FieldOfView;
			if (AActor* SkyActor = Terrain->SpaceNebula->GetOwner()) SkyActor->SetActorLocation(Location);
		}

		void UpdateScene(float DeltaSeconds)
		{
			const FVector Viewpoint = Capture->GetComponentLocation();
			Terrain->SpaceNebula->Update(Viewpoint, Terrain->SpaceLights, DeltaSeconds);
			Terrain->SpacePlanet->Update(DeltaSeconds, Viewpoint);
			Terrain->SpaceDust->Update(Viewpoint, Terrain->SpaceLights, DeltaSeconds);
		}

		/** Captures the whole scene and saves it; returns the count of lit pixels. */
		int32 Shoot(FAutomationTestBase& Test, const FString& Path)
		{
			TArray<FColor> Pixels;
			return ShootPixels(Test, Path, Pixels);
		}

		/** As Shoot, keeping the pixels. */
		int32 ShootPixels(FAutomationTestBase& Test, const FString& Path, TArray<FColor>& Pixels)
		{
			World->SendAllEndOfFrameUpdates(); Capture->CaptureScene();
			if (!Test.TestTrue(TEXT("render target readable"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return 0;
			TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(CaptureWidth, CaptureHeight, Pixels, Png);
			FFileHelper::SaveArrayToFile(Png, *Path);
			int32 Lit = 0;
			for (const FColor& Pixel : Pixels) Lit += Pixel.R > 20 || Pixel.G > 20 || Pixel.B > 20;
			return Lit;
		}
	};
namespace{	FString CaptureDirectory()	{		const FString Directory = FPaths::ProjectDir()/TEXT(".agent_tmp/terrain-quality-captures");		IFileManager::Get().MakeDirectory(*Directory, true);		return Directory;	}}

namespace
{
	/** Luminance rings around the brightest spot near the image centre: the mean and the coefficient of variation (std / mean) of each ring. */
	struct FSunProfile
	{
		TArray<double> RingMean, RingVariation;
		double CentreX = 0, CentreY = 0, Peak = 0;
		/** Largest over smallest radius at which the bright core (90% of the peak) ends, over 36 directions; 1 is a perfect circle. */
		double CoreRoundness = 0;
		double CoreRadius = 0;
	};

	FSunProfile MeasureSun(const TArray<FColor>& Pixels, int32 Width, int32 Height, int32 MaxRadius, int32 RadiusStep)
	{
		FSunProfile Profile;
		auto Luma = [&](double X, double Y)
		{
			const int32 Column = FMath::Clamp(FMath::RoundToInt(X), 0, Width - 1), Row = FMath::Clamp(FMath::RoundToInt(Y), 0, Height - 1);
			const FColor& Pixel = Pixels[Row*Width + Column];
			return .3*Pixel.R + .59*Pixel.G + .11*Pixel.B;
		};
		for (int32 Row = Height/2 - 60; Row < Height/2 + 60; ++Row)
			for (int32 Column = Width/2 - 60; Column < Width/2 + 60; ++Column) Profile.Peak = FMath::Max(Profile.Peak, Luma(Column, Row));
		double SumX = 0, SumY = 0, Count = 0;
		for (int32 Row = Height/2 - 60; Row < Height/2 + 60; ++Row)
			for (int32 Column = Width/2 - 60; Column < Width/2 + 60; ++Column)
				if (Luma(Column, Row) >= Profile.Peak*.97) { SumX += Column; SumY += Row; Count += 1; }
		Profile.CentreX = Count > 0 ? SumX/Count : Width*.5; Profile.CentreY = Count > 0 ? SumY/Count : Height*.5;
		constexpr int32 AngleCount = 96;
		for (int32 Radius = RadiusStep; Radius <= MaxRadius; Radius += RadiusStep)
		{
			double Sum = 0, SumSquares = 0;
			for (int32 AngleIndex = 0; AngleIndex < AngleCount; ++AngleIndex)
			{
				const double Angle = AngleIndex*UE_TWO_PI/AngleCount;
				const double Value = Luma(Profile.CentreX + Radius*FMath::Cos(Angle), Profile.CentreY + Radius*FMath::Sin(Angle));
				Sum += Value; SumSquares += Value*Value;
			}
			const double Mean = Sum/AngleCount, Variance = FMath::Max(SumSquares/AngleCount - Mean*Mean, 0.0);
			Profile.RingMean.Add(Mean); Profile.RingVariation.Add(Mean > 1.0 ? FMath::Sqrt(Variance)/Mean : 0.0);
		}
		double Shortest = TNumericLimits<double>::Max(), Longest = 0;
		for (int32 Direction = 0; Direction < 36; ++Direction)
		{
			const double Angle = Direction*UE_TWO_PI/36.0;
			double Radius = 0;
			while (Radius < MaxRadius && Luma(Profile.CentreX + Radius*FMath::Cos(Angle), Profile.CentreY + Radius*FMath::Sin(Angle)) >= Profile.Peak*.9) Radius += .5;
			Shortest = FMath::Min(Shortest, Radius); Longest = FMath::Max(Longest, Radius);
			Profile.CoreRadius += Radius/36.0;
		}
		Profile.CoreRoundness = Shortest > 0.5 ? Longest/Shortest : 99.0;
		return Profile;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceZoneCaptureTest, "SWG.Space.ZoneCaptures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
bool FSWGSpaceZoneCaptureTest::RunTest(const FString& Parameters)
{
	FSWGSpaceZoneFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!TestTrue(TEXT("space fixture starts"), Fixture.Init(*this))) return false;
	const FString Directory = CaptureDirectory();
	// The Stage 9 views this pass is compared against stay in terrain-globe-captures; copy them to before/ once.
	{
		const FString BeforeDirectory = Directory/TEXT("before");
		const FString StageNineDirectory = FPaths::ProjectDir()/TEXT(".agent_tmp/terrain-globe-captures");
		IFileManager::Get().MakeDirectory(*BeforeDirectory, true);
		for (const TCHAR* Name : {TEXT("tatooine-1-planet.png"), TEXT("tatooine-1b-surface-region.png"), TEXT("naboo-1-planet.png"), TEXT("naboo-1b-surface-region.png"),
			TEXT("corellia-1-planet.png"), TEXT("corellia-1b-surface-region.png")})
			if (!IFileManager::Get().FileExists(*(BeforeDirectory/Name)) && IFileManager::Get().FileExists(*(StageNineDirectory/Name))) IFileManager::Get().Copy(*(BeforeDirectory/Name), *(StageNineDirectory/Name));
	}
	for (const TCHAR* Zone : {TEXT("tatooine"), TEXT("naboo"), TEXT("corellia"), TEXT("lok"), TEXT("endor"), TEXT("dathomir"), TEXT("yavin4"), TEXT("dantooine")})
	{
		const FString Label(Zone);
		if (!TestTrue(*(Label + TEXT(" loads")), Fixture.LoadZone(Label))) continue;
		USWGSpacePlanetComponent* Planets = Fixture.Planet();
		const int32 PrimaryIndex = Planets->GetRegion().PlanetIndex != INDEX_NONE ? Planets->GetRegion().PlanetIndex : 0;
		if (!TestTrue(*(Label + TEXT(" has a primary planet")), Planets->Planets.IsValidIndex(PrimaryIndex) && IsValid(Planets->Planets[PrimaryIndex].Surface.Get()))) continue;
		// Every primary planet whose ground terrain exists takes the region, single-texture shaders included.
		TestTrue(*(Label + TEXT(" region is ready")), Planets->GetRegion().bReady && Planets->GetRegion().PlanetIndex == PrimaryIndex);

		// Launch view: the scene origin, looking at the primary planet.
		const FVector PlanetLocation = Planets->Planets[PrimaryIndex].Surface->GetComponentLocation();
		Fixture.Pilot->SetActorLocation(FVector::ZeroVector);
		Fixture.PlaceCamera(FVector::ZeroVector, PlanetLocation.Rotation(), 80.f);
		Fixture.UpdateScene(FrameSeconds); Fixture.Pump(3);
		const int32 PlanetLit = Fixture.Shoot(*this, Directory/(Label + TEXT("-1-planet.png")));
		TestTrue(*(Label + TEXT(" planet view renders")), PlanetLit > 5000);
		// Clouds and haze at their default thinning (the shot above) against the retail clouds untouched, over the primary planet.
		if (Planets->GetRegion().bReady)
		{
			IConsoleVariable* Thinning = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.PlanetCloudThinning"));
			if (TestNotNull(*(Label + TEXT(" has the cloud thinning cvar")), Thinning))
			{
				const float StoredThinning = Thinning->GetFloat();
				Thinning->Set(0.f, ECVF_SetByCode);
				Fixture.UpdateScene(FrameSeconds); Fixture.Pump(2);
				TestTrue(*(Label + TEXT(" planet view without thinning renders")), Fixture.Shoot(*this, Directory/(Label + TEXT("-1e-planet-no-thinning.png"))) > 5000);
				Thinning->Set(StoredThinning, ECVF_SetByCode);
				Fixture.UpdateScene(FrameSeconds);
			}
		}
		// The globe alone, with and without its terrain, from the launch point (haze and clouds hidden), then from outside on the far side.
		if (Planets->GetRegion().bReady)
		{
			const USWGSpacePlanetComponent::FPlanetVisual& Primary = Planets->Planets[PrimaryIndex];
			Fixture.Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			Fixture.Capture->ShowOnlyComponents.Reset(); Fixture.Capture->ShowOnlyComponent(Primary.Surface);
			TArray<FColor> GlobePixels, PlainPixels;
			const int32 RegionLit = Fixture.ShootPixels(*this, Directory/(Label + TEXT("-1b-surface-region.png")), GlobePixels);
			Primary.SurfaceMaterial->SetScalarParameterValue(TEXT("RegionEnabled"), 0.f);
			const int32 PlainLit = Fixture.ShootPixels(*this, Directory/(Label + TEXT("-1c-surface-no-region.png")), PlainPixels);
			Primary.SurfaceMaterial->SetScalarParameterValue(TEXT("RegionEnabled"), 1.f);
			TestTrue(*(Label + TEXT(" globe renders with and without its terrain")), RegionLit > 5000 && PlainLit > 5000);
			int32 Different = 0;
			for (int32 Index = 0; Index < FMath::Min(GlobePixels.Num(), PlainPixels.Num()); ++Index)
				Different += FMath::Abs(int32(GlobePixels[Index].R)-int32(PlainPixels[Index].R)) > 12 || FMath::Abs(int32(GlobePixels[Index].G)-int32(PlainPixels[Index].G)) > 12
					|| FMath::Abs(int32(GlobePixels[Index].B)-int32(PlainPixels[Index].B)) > 12;
			AddInfo(FString::Printf(TEXT("%s launch view, surface alone: %d lit pixels, %d differ from the generic surface"), Zone, RegionLit, Different));
			TestTrue(*(Label + TEXT(" terrain replaces most of the generic surface in the launch view")), Different > RegionLit/2);

			// Stage 10: the same view with the ground detail textures off (the bake alone), to see and count what the detail adds.
			if (IConsoleVariable* DetailStrength = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.PlanetDetailStrength")))
			{
				const float StoredStrength = DetailStrength->GetFloat();
				DetailStrength->Set(0.f, ECVF_SetByCode);
				Fixture.UpdateScene(FrameSeconds);
				TArray<FColor> BakeOnlyPixels;
				Fixture.ShootPixels(*this, Directory/(Label + TEXT("-1f-surface-detail-off.png")), BakeOnlyPixels);
				DetailStrength->Set(StoredStrength, ECVF_SetByCode);
				Fixture.UpdateScene(FrameSeconds);
				int32 DetailDifferent = 0;
				for (int32 Index = 0; Index < FMath::Min(GlobePixels.Num(), BakeOnlyPixels.Num()); ++Index)
					DetailDifferent += FMath::Abs(int32(GlobePixels[Index].R) - int32(BakeOnlyPixels[Index].R)) + FMath::Abs(int32(GlobePixels[Index].G) - int32(BakeOnlyPixels[Index].G))
						+ FMath::Abs(int32(GlobePixels[Index].B) - int32(BakeOnlyPixels[Index].B)) > 18;
				AddInfo(FString::Printf(TEXT("%s launch view: %d of %d lit pixels change by more than 6 levels a channel with the ground detail"), Zone, DetailDifferent, RegionLit));
				TestTrue(*(Label + TEXT(" the retail ground detail textures change the launch view")), Planets->GetRegion().DetailFamilyCount == 0 || DetailDifferent > RegionLit/200);
			}

			// The far side from outside, the key light turned to shine on it.
			const FVector Direction = PlanetLocation.GetSafeNormal();
			const float PlanetRadius = Primary.Surface->Bounds.BoxExtent.GetMax();
			ADirectionalLight* KeyLight = Fixture.Lights().IsEmpty() ? nullptr : Fixture.Lights()[0].Get();
			const FRotator StoredLightRotation = KeyLight ? KeyLight->GetActorRotation() : FRotator::ZeroRotator;
			if (KeyLight) KeyLight->SetActorRotation((-Direction).Rotation());
			Fixture.UpdateScene(FrameSeconds);
			Fixture.Capture->SetWorldLocation(PlanetLocation + Direction*PlanetRadius*2.4); Fixture.Capture->SetWorldRotation((-Direction).Rotation()); Fixture.Capture->FOVAngle = 34.f;
			TestTrue(*(Label + TEXT(" far side renders")), Fixture.Shoot(*this, Directory/(Label + TEXT("-1d-far-side.png"))) > 5000);
			if (KeyLight) KeyLight->SetActorRotation(StoredLightRotation);
			Fixture.Capture->ShowOnlyComponents.Reset(); Fixture.Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
			Fixture.PlaceCamera(FVector::ZeroVector, PlanetLocation.Rotation(), 80.f);
			Fixture.UpdateScene(FrameSeconds);
		}

		// Stage 10: the sun, close up with only the celestial sprites drawn, in the code-built form and the retail cross texture. The sun must be a round disc whose glow falls off
		// smoothly with no spikes: low luminance variation round each ring (angular) against a steady fall with radius.
		if (Label == TEXT("tatooine"))
		{
			const int32 SunIndex = Planets->GetMainSunIndex();
			if (TestTrue(TEXT("Tatooine has a main sun drawn by the code-built material"), SunIndex != INDEX_NONE && Planets->Celestials.IsValidIndex(SunIndex)))
			{
				const FVector SunDirection = (Planets->Celestials[SunIndex].Sprite->GetComponentLocation() - Fixture.Capture->GetComponentLocation()).GetSafeNormal();
				Fixture.PlaceCamera(FVector::ZeroVector, SunDirection.Rotation(), 20.f);
				Fixture.UpdateScene(FrameSeconds); Fixture.Pump(3);
				Fixture.Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
				Fixture.Capture->ShowOnlyComponents.Reset();
				// Only the cross-texture sprites (code-built or retail): the retail radial glow floods a 20 degree view and would hide the disc.
				for (const USWGSpacePlanetComponent::FCelestialVisual& Celestial : Planets->Celestials) if (Celestial.StarMaterial) Fixture.Capture->ShowOnlyComponent(Celestial.Sprite);
				TArray<FColor> SunPixels, RetailPixels;
				const int32 SunLit = Fixture.ShootPixels(*this, Directory/TEXT("sun-closeup.png"), SunPixels);
				// The retail sprites for comparison: the same sun with its original cross texture materials.
				for (const USWGSpacePlanetComponent::FCelestialVisual& Celestial : Planets->Celestials)
					if (Celestial.StarMaterial && Celestial.RetailMaterial) Celestial.Sprite->SetMaterial(0, Celestial.RetailMaterial);
				Fixture.Pump(2);
				Fixture.ShootPixels(*this, Directory/TEXT("sun-closeup-before-retail-cross.png"), RetailPixels);
				for (const USWGSpacePlanetComponent::FCelestialVisual& Celestial : Planets->Celestials)
					if (Celestial.StarMaterial) Celestial.Sprite->SetMaterial(0, Celestial.StarMaterial);
				Fixture.Capture->ShowOnlyComponents.Reset(); Fixture.Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
				TestTrue(TEXT("the sun close-up renders"), SunLit > 200);
				// The same sun with every layer and at a wide field of view, for the eye.
				Fixture.PlaceCamera(FVector::ZeroVector, SunDirection.Rotation(), 70.f);
				Fixture.UpdateScene(FrameSeconds); Fixture.Pump(2);
				Fixture.Shoot(*this, Directory/TEXT("sun-wide-all-layers.png"));

				const FSunProfile After = MeasureSun(SunPixels, CaptureWidth, CaptureHeight, 220, 4), Before = MeasureSun(RetailPixels, CaptureWidth, CaptureHeight, 220, 4);
				auto Summarise = [](const FSunProfile& Profile, double& OutMeanVariation, double& OutMaxVariation, int32& OutRises, int32& OutRings)
				{
					OutMeanVariation = 0; OutMaxVariation = 0; OutRises = 0; OutRings = 0;
					double PreviousMean = -1;
					for (int32 Ring = 0; Ring < Profile.RingMean.Num() && Profile.RingMean[Ring] >= 16.0; ++Ring)
					{
						OutMeanVariation += Profile.RingVariation[Ring]; OutMaxVariation = FMath::Max(OutMaxVariation, Profile.RingVariation[Ring]); ++OutRings;
						if (PreviousMean >= 0 && Profile.RingMean[Ring] > PreviousMean*1.03 + 1.0) ++OutRises;
						PreviousMean = Profile.RingMean[Ring];
					}
					if (OutRings > 0) OutMeanVariation /= OutRings;
				};
				double AfterMean, AfterMax, BeforeMean, BeforeMax; int32 AfterRises, AfterRings, BeforeRises, BeforeRings;
				Summarise(After, AfterMean, AfterMax, AfterRises, AfterRings); Summarise(Before, BeforeMean, BeforeMax, BeforeRises, BeforeRings);
				double RadialRange = 0;
				if (After.RingMean.Num() > 1) RadialRange = (After.RingMean[0] - After.RingMean.Last())/FMath::Max(After.RingMean[0], 1.0);
				AddInfo(FString::Printf(TEXT("sun (code-built): centre (%.1f, %.1f), peak %.0f, %d glow rings, angular variation mean %.3f max %.3f, radial fall %.2f, %d rises, core radius %.1f px, roundness %.2f"),
					After.CentreX, After.CentreY, After.Peak, AfterRings, AfterMean, AfterMax, RadialRange, AfterRises, After.CoreRadius, After.CoreRoundness));
				AddInfo(FString::Printf(TEXT("sun (retail cross, before): peak %.0f, %d glow rings, angular variation mean %.3f max %.3f, %d rises, core radius %.1f px, roundness %.2f"),
					Before.Peak, BeforeRings, BeforeMean, BeforeMax, BeforeRises, Before.CoreRadius, Before.CoreRoundness));
				TestTrue(*FString::Printf(TEXT("the sun's glow reaches out over many rings (%d)"), AfterRings), AfterRings >= 8);
				TestTrue(*FString::Printf(TEXT("the sun has no spikes: angular luminance variation is low (mean %.3f, max %.3f)"), AfterMean, AfterMax), AfterMean < .08 && AfterMax < .2);
				TestTrue(*FString::Printf(TEXT("the glow falls off smoothly with radius (%d rises, fall %.2f)"), AfterRises, RadialRange), AfterRises <= 2 && RadialRange > .1);
				TestTrue(*FString::Printf(TEXT("the sun's bright core is a round disc (%.1f px, roundness %.2f)"), After.CoreRadius, After.CoreRoundness), After.CoreRadius >= 2.0 && After.CoreRoundness <= 1.25);
			}
		}

		// Nebula from outside, at 2.8 radii, inside the near LOD.
		if (TestFalse(*(Label + TEXT(" has a nebula")), Fixture.Nebula()->Nebulas.IsEmpty()))
		{
			// The clearest outside view: the nebula and approach direction whose camera spot sits in the least gas from the other clouds.
			const USWGSpaceNebulaComponent::FNebulaVisual* Chosen = nullptr;
			FVector ChosenDirection = FVector::ForwardVector;
			double BestOverlap = TNumericLimits<double>::Max();
			for (const USWGSpaceNebulaComponent::FNebulaVisual& Candidate : Fixture.Nebula()->Nebulas)
				for (const FVector& Direction : {FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0), FVector(0, 0, 1), FVector(0, 0, -1)})
				{
					const FVector Spot = Candidate.SpacePosition - Direction*Candidate.Radius*2.8;
					double Overlap = 0;
					for (const USWGSpaceNebulaComponent::FNebulaVisual& Other : Fixture.Nebula()->Nebulas)
						Overlap += FMath::Max(0.0, 1.0 - FVector::Dist(Spot, Other.SpacePosition)/Other.Radius)*Other.Radius;
					if (Overlap < BestOverlap) { BestOverlap = Overlap; Chosen = &Candidate; ChosenDirection = Direction; }
				}
			const USWGSpaceNebulaComponent::FNebulaVisual& Nebula = *Chosen;
			const FVector ToNebula = ChosenDirection;
			Fixture.PlaceCamera(Nebula.SpacePosition - ToNebula*Nebula.Radius*2.8, ToNebula.Rotation(), 70.f);
			Fixture.UpdateScene(FrameSeconds); Fixture.Pump(3);
			const int32 NebulaLit = Fixture.Shoot(*this, Directory/(Label + TEXT("-2-nebula.png")));
			TestTrue(*(Label + TEXT(" nebula view renders")), NebulaLit > 5000);
		}

		// Dust at cruise: the pilot covers 20 m per frame, so the streak sits at its one metre cap.
		Fixture.PlaceCamera(FVector::ZeroVector, FRotator::ZeroRotator, 80.f);
		Fixture.Pilot->SetActorLocation(FVector::ZeroVector);
		Fixture.UpdateScene(FrameSeconds); Fixture.UpdateScene(FrameSeconds);
		Fixture.Pilot->SetActorLocation(FVector(2000, 0, 0));
		Fixture.UpdateScene(FrameSeconds);
		TestTrue(*(Label + TEXT(" dust streaks at cruise")), Fixture.Dust()->GetStreakVector().Size() > 50.0);
		const int32 DustLit = Fixture.Shoot(*this, Directory/(Label + TEXT("-3-dust-cruise.png")));
		TestTrue(*(Label + TEXT(" dust renders")), DustLit > 50);
		AddInfo(FString::Printf(TEXT("%s: planet lit %d, dust lit %d, region %s"), Zone, PlanetLit, DustLit, Planets->GetRegion().bFromCache ? TEXT("cached") : TEXT("baked")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceCpuBudgetTest, "SWG.Space.CpuBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
bool FSWGSpaceCpuBudgetTest::RunTest(const FString& Parameters)
{
	FSWGSpaceZoneFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!TestTrue(TEXT("space fixture starts"), Fixture.Init(*this)) || !TestTrue(TEXT("Tatooine loads"), Fixture.LoadZone(TEXT("tatooine")))) return false;
	USWGSpaceDustComponent* Dust = Fixture.Dust();
	USWGSpaceNebulaComponent* Nebula = Fixture.Nebula();
	USWGSpacePlanetComponent* Planets = Fixture.Planet();
	if (!TestTrue(TEXT("Tatooine has nebulae and asteroid fields"), !Nebula->Nebulas.IsEmpty() && !Dust->Fields.IsEmpty())) return false;

	// The debris layer only runs inside an asteroid field, so measure from its centre once the rock meshes are in.
	const FSWGAsteroidFieldData& Field = Dust->Fields[0];
	const FVector NativeSpot = Field.bSpline && !Field.ControlPoints.IsEmpty() ? Field.ControlPoints[0] : Field.Center;
	const FVector FieldCentre = FVector(NativeSpot.Z, NativeSpot.X, NativeSpot.Y)*SWGWorldScale;
	for (int32 Attempt = 0; Attempt < 1500; ++Attempt)
	{
		bool bLoaded = Dust->DebrisMeshes.Num() > 0;
		for (const TObjectPtr<UInstancedStaticMeshComponent>& Layer : Dust->DebrisMeshes) bLoaded &= Layer != nullptr;
		if (bLoaded) break;
		Fixture.Pump(1); FPlatformProcess::Sleep(.01f);
	}
	Fixture.Pilot->SetActorLocation(FieldCentre);
	Fixture.PlaceCamera(FieldCentre, FRotator::ZeroRotator, 80.f);

	struct FCost { const TCHAR* Name; double AverageMs = 0, WorstMs = 0; };
	TArray<FCost> Costs;
	constexpr int32 Warmup = 30, Samples = 600;
	auto Measure = [&](const TCHAR* Name, TFunctionRef<void(int32)> Work)
	{
		FCost Cost{Name};
		double Total = 0;
		for (int32 Index = 0; Index < Warmup + Samples; ++Index)
		{
			const double Start = FPlatformTime::Seconds();
			Work(Index);
			const double Milliseconds = (FPlatformTime::Seconds()-Start)*1000.0;
			if (Index < Warmup) continue;
			Total += Milliseconds; Cost.WorstMs = FMath::Max(Cost.WorstMs, Milliseconds);
		}
		Cost.AverageMs = Total/Samples;
		Costs.Add(Cost);
	};
	// Cruise: 20 m per frame along the field, so the camera keeps moving and particles keep respawning.
	Measure(TEXT("Dust + debris Update (750 dust, field-style rocks, cruise)"), [&](int32 Index)
	{
		const FVector Position = FieldCentre + FVector(Index*2000.0, 0, 0);
		Fixture.Pilot->SetActorLocation(Position); Fixture.Capture->SetWorldLocation(Position);
		Dust->Update(Position, Fixture.Lights(), FrameSeconds);
	});
	const USWGSpaceNebulaComponent::FNebulaVisual& First = Nebula->Nebulas[0];
	Fixture.Capture->SetWorldLocation(First.SpacePosition - First.SpacePosition.GetSafeNormal()*First.Radius*2.2);
	Measure(TEXT("Nebula Update (outside, all Tatooine volumes)"), [&](int32) { Nebula->Update(Fixture.Capture->GetComponentLocation(), Fixture.Lights(), FrameSeconds); });
	Fixture.Capture->SetWorldLocation(First.SpacePosition);
	Measure(TEXT("Nebula Update (inside: tint, wisps, lightning)"), [&](int32) { Nebula->Update(Fixture.Capture->GetComponentLocation(), Fixture.Lights(), FrameSeconds); });
	Measure(TEXT("Planet Update (spin, sun, materials)"), [&](int32) { Planets->Update(FrameSeconds, Fixture.Capture->GetComponentLocation()); });

	FString Report = TEXT("CPU per frame, Development editor build, game thread, 600 frames after 30 warm-up\n");
	for (const FCost& Cost : Costs)
	{
		Report += FString::Printf(TEXT("%-58s avg %.4f ms  worst %.4f ms\n"), Cost.Name, Cost.AverageMs, Cost.WorstMs);
		AddInfo(FString::Printf(TEXT("CPU %s: avg %.4f ms, worst %.4f ms"), Cost.Name, Cost.AverageMs, Cost.WorstMs));
		TestTrue(*FString::Printf(TEXT("%s stays under 1 ms on average"), Cost.Name), Cost.AverageMs < 1.0);
	}
	FFileHelper::SaveStringToFile(Report, *(CaptureDirectory()/TEXT("cpu-budget.txt")));
	return true;
}

/** Debris meshes load asynchronously: tick the world until their layers exist, then render them inside a field and look for lit rock pixels. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceDebrisRenderTest, "SWG.Space.DebrisRender", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
bool FSWGSpaceDebrisRenderTest::RunTest(const FString& Parameters)
{
	FSWGSpaceZoneFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!TestTrue(TEXT("space fixture starts"), Fixture.Init(*this)) || !TestTrue(TEXT("Tatooine loads"), Fixture.LoadZone(TEXT("tatooine")))) return false;
	USWGSpaceDustComponent* Dust = Fixture.Dust();
	if (!TestTrue(TEXT("debris is on at the default quality tier"), IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.Debris"))->GetBool() && !Dust->Debris.IsEmpty())) return false;

	bool bLoaded = false;
	const double WaitStart = FPlatformTime::Seconds();
	while (!bLoaded && FPlatformTime::Seconds()-WaitStart < 60.0)
	{
		Fixture.Pump(1);
		bLoaded = Dust->DebrisMeshes.Num() > 0;
		for (const TObjectPtr<UInstancedStaticMeshComponent>& Layer : Dust->DebrisMeshes) bLoaded &= Layer != nullptr;
		if (!bLoaded) FPlatformProcess::Sleep(.01f);
	}
	if (!TestTrue(TEXT("every debris mesh layer exists within the timeout"), bLoaded)) return false;
	AddInfo(FString::Printf(TEXT("debris meshes ready after %.2f s"), FPlatformTime::Seconds()-WaitStart));
	// The layers' materials arrived with the meshes; compile only what is still missing.
	Fixture.PrepareShaders();
	Fixture.Pump(5);

	const FSWGAsteroidFieldData& Field = Dust->Fields[0];
	const FVector NativeSpot = Field.bSpline && !Field.ControlPoints.IsEmpty() ? Field.ControlPoints[0] : Field.Center;
	const FVector FieldCentre = FVector(NativeSpot.Z, NativeSpot.X, NativeSpot.Y)*SWGWorldScale;
	Fixture.Pilot->SetActorLocation(FieldCentre);
	Fixture.PlaceCamera(FieldCentre, FRotator::ZeroRotator, 80.f);
	Fixture.UpdateScene(FrameSeconds); Fixture.UpdateScene(FrameSeconds);
	if (!TestTrue(TEXT("debris is active inside the field"), Dust->IsDebrisActive())) return false;

	// Aim at the nearest shown rock.
	FVector NearestRock = FVector::ZeroVector; double NearestDistance = TNumericLimits<double>::Max(), NearestSize = 0; int32 ShownRocks = 0;
	for (const TObjectPtr<UInstancedStaticMeshComponent>& Layer : Dust->DebrisMeshes)
		for (int32 Instance = 0; Instance < Layer->GetInstanceCount(); ++Instance)
		{
			FTransform Transform; Layer->GetInstanceTransform(Instance, Transform, true);
			if (Transform.GetScale3D().IsNearlyZero()) continue;
			++ShownRocks;
			const double Distance = FVector::Dist(Transform.GetLocation(), FieldCentre);
			if (Distance < NearestDistance) { NearestDistance = Distance; NearestRock = Transform.GetLocation(); NearestSize = Transform.GetScale3D().X*Layer->GetStaticMesh()->GetBounds().SphereRadius*2.0; }
		}
	TestEqual(TEXT("the active style's rocks are shown"), ShownRocks, Dust->GetActiveDebrisStyle().Count);
	if (!TestTrue(TEXT("a rock is within the style's spawn range"), NearestDistance <= Dust->GetActiveDebrisStyle().SpawnRange+1.0)) return false;
	AddInfo(FString::Printf(TEXT("%d rocks shown; nearest %.1f m away, %.1f m across"), ShownRocks, NearestDistance/SWGWorldScale, NearestSize/SWGWorldScale));
	Fixture.Capture->SetWorldRotation((NearestRock-FieldCentre).Rotation());

	Fixture.Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Fixture.Capture->ShowOnlyComponents.Reset();
	for (const TObjectPtr<UInstancedStaticMeshComponent>& Layer : Dust->DebrisMeshes) Fixture.Capture->ShowOnlyComponent(Layer);
	TArray<FColor> Pixels;
	const int32 Lit = Fixture.ShootPixels(*this, CaptureDirectory()/TEXT("debris-in-field.png"), Pixels);
	int32 Bright = 0;
	for (const FColor& Pixel : Pixels) Bright += Pixel.R > 60 || Pixel.G > 60 || Pixel.B > 60;
	AddInfo(FString::Printf(TEXT("debris lit pixels %d, bright (>60) %d"), Lit, Bright));
	TestTrue(TEXT("lit debris pixels render inside the field"), Lit > 400);
	TestTrue(TEXT("the rocks are shaded, not flat black silhouettes"), Bright > 50);

	// Control: with the layers hidden the same view is empty.
	for (const TObjectPtr<UInstancedStaticMeshComponent>& Layer : Dust->DebrisMeshes) Layer->SetVisibility(false);
	TArray<FColor> ControlPixels;
	const int32 ControlLit = Fixture.ShootPixels(*this, CaptureDirectory()/TEXT("debris-hidden-control.png"), ControlPixels);
	TestEqual(TEXT("hidden debris leaves the view empty"), ControlLit, 0);
	Fixture.Capture->ShowOnlyComponents.Reset(); Fixture.Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	return true;
}

/** Naboo's art-table ocean flag: a near-specular sun view of the globe with the glint off and on, to see where the dark-albedo heuristic puts the highlight. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceOceanGlintTest, "SWG.Space.OceanGlint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
bool FSWGSpaceOceanGlintTest::RunTest(const FString& Parameters)
{
	FSWGSpaceZoneFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!TestTrue(TEXT("space fixture starts"), Fixture.Init(*this)) || !TestTrue(TEXT("Naboo loads"), Fixture.LoadZone(TEXT("naboo")))) return false;
	USWGSpacePlanetComponent* Planets = Fixture.Planet();
	const int32 PrimaryIndex = Planets->GetRegion().PlanetIndex != INDEX_NONE ? Planets->GetRegion().PlanetIndex : 0;
	if (!TestTrue(TEXT("Naboo has a primary planet surface"), Planets->Planets.IsValidIndex(PrimaryIndex) && Planets->Planets[PrimaryIndex].SurfaceMaterial && Fixture.Lights().Num() > 0)) return false;
	const USWGSpacePlanetComponent::FPlanetVisual& Primary = Planets->Planets[PrimaryIndex];
	float GlintFlag = 0;
	Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OceanGlint")), GlintFlag);
	TestEqual(TEXT("Naboo's art table flags an ocean world"), GlintFlag, 1.f);
	// Stage 10: land is fully rough and non-specular (no plastic sheen); the glint below comes from the water alone.
	float LandRoughness = -1, LandSpecular = -1, WaterRoughness = -1, WaterSpecular = -1;
	Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("LandRoughness")), LandRoughness);
	Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("LandSpecular")), LandSpecular);
	Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("WaterRoughness")), WaterRoughness);
	Primary.SurfaceMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("WaterSpecular")), WaterSpecular);
	TestTrue(*FString::Printf(TEXT("land roughness %.2f is high and specular %.2f is low"), LandRoughness, LandSpecular), LandRoughness >= .95f && LandSpecular <= .1f);
	TestTrue(*FString::Printf(TEXT("water keeps a glossy specular surface (roughness %.2f, specular %.2f)"), WaterRoughness, WaterSpecular), WaterRoughness <= .25f && WaterSpecular >= .3f);
#if WITH_EDITORONLY_DATA
	if (UMaterialInterface* CloudParent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGPlanetCloud.M_SWGPlanetCloud")))
	{
		// The cloud's roughness and specular are constants wired into the base material.
		UMaterial* CloudBase = CloudParent->GetMaterial();
		UMaterialEditorOnlyData* CloudInputs = CloudBase ? CloudBase->GetEditorOnlyData() : nullptr;
		const UMaterialExpressionConstant* RoughnessConstant = CloudInputs ? Cast<UMaterialExpressionConstant>(CloudInputs->Roughness.Expression) : nullptr;
		const UMaterialExpressionConstant* SpecularConstant = CloudInputs ? Cast<UMaterialExpressionConstant>(CloudInputs->Specular.Expression) : nullptr;
		TestTrue(TEXT("the cloud material is fully rough and non-specular"), RoughnessConstant && SpecularConstant && RoughnessConstant->R >= .95f && SpecularConstant->R <= .1f);
	}
#endif

	Fixture.PlaceCamera(FVector::ZeroVector, FRotator::ZeroRotator, 60.f);
	Fixture.UpdateScene(FrameSeconds);
	// A camera 50 degrees round from the sun sees the mirror point halfway between, 25 degrees off its own axis.
	const FVector ToSun = -Fixture.Lights()[0]->GetActorForwardVector();
	FVector Axis = FVector::CrossProduct(ToSun, FVector::UpVector);
	if (Axis.IsNearlyZero()) Axis = FVector::RightVector;
	const FVector ToCamera = ToSun.RotateAngleAxis(50.0, Axis.GetSafeNormal());
	const FVector Centre = Primary.Surface->GetComponentLocation();
	const double Radius = Primary.Surface->Bounds.SphereRadius;
	Fixture.Capture->SetWorldLocation(Centre + ToCamera*Radius*3.2);
	Fixture.Capture->SetWorldRotation((-ToCamera).Rotation());
	Fixture.UpdateScene(FrameSeconds); Fixture.Pump(2);
	Fixture.Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Fixture.Capture->ShowOnlyComponents.Reset(); Fixture.Capture->ShowOnlyComponent(Primary.Surface);

	TArray<FColor> Off, On;
	Primary.SurfaceMaterial->SetScalarParameterValue(TEXT("OceanGlint"), 0.f);
	Fixture.ShootPixels(*this, CaptureDirectory()/TEXT("naboo-glint-off.png"), Off);
	Primary.SurfaceMaterial->SetScalarParameterValue(TEXT("OceanGlint"), 1.f);
	Fixture.ShootPixels(*this, CaptureDirectory()/TEXT("naboo-glint-on.png"), On);
	Fixture.Capture->ShowOnlyComponents.Reset(); Fixture.Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	if (!TestEqual(TEXT("both captures read back"), Off.Num(), On.Num()) || Off.IsEmpty()) return false;

	// Where the glint adds light, and what the globe looks like there with the glint off.
	TArray<FColor> Difference; Difference.SetNumZeroed(On.Num());
	int32 GlintPixels = 0, WaterLike = 0; double Brightest = 0; FVector2D Centroid = FVector2D::ZeroVector; FLinearColor MeanAlbedo(0, 0, 0);
	auto Luma = [](const FColor& Pixel) { return .3*Pixel.R + .59*Pixel.G + .11*Pixel.B; };
	for (int32 Index = 0; Index < On.Num(); ++Index)
	{
		const double Gain = Luma(On[Index]) - Luma(Off[Index]);
		const uint8 Amplified = uint8(FMath::Clamp(Gain*4.0, 0.0, 255.0));
		Difference[Index] = FColor(Amplified, Amplified, Amplified, 255);
		Brightest = FMath::Max(Brightest, Gain);
		if (Gain < 20.0) continue;
		++GlintPixels; Centroid += FVector2D(Index%CaptureWidth, Index/CaptureWidth);
		MeanAlbedo += FLinearColor(Off[Index].R/255.f, Off[Index].G/255.f, Off[Index].B/255.f);
		WaterLike += Off[Index].B >= Off[Index].R && Off[Index].B*1.1 >= Off[Index].G;
	}
	TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(CaptureWidth, CaptureHeight, Difference, Png);
	FFileHelper::SaveArrayToFile(Png, *(CaptureDirectory()/TEXT("naboo-glint-difference.png")));
	if (GlintPixels > 0) { Centroid /= GlintPixels; MeanAlbedo = MeanAlbedo/float(GlintPixels); }
	AddInfo(FString::Printf(TEXT("Naboo glint: %d pixels gain >20 (peak +%.0f), centroid (%.0f, %.0f), mean colour there with glint off (%.2f, %.2f, %.2f), blue-dominant %d%%"),
		GlintPixels, Brightest, Centroid.X, Centroid.Y, MeanAlbedo.R, MeanAlbedo.G, MeanAlbedo.B, GlintPixels ? WaterLike*100/GlintPixels : 0));
	TestTrue(TEXT("the near-specular sun view shows a glint on Naboo's dark-albedo areas"), GlintPixels > 50);
	return true;
}
#endif
