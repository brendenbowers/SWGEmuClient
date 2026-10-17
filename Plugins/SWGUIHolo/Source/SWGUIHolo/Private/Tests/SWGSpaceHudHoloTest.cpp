#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "MaterialShared.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "SceneInterface.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Common/SWGWorldScale.h"
#include "Objects/World/SWGShip.h"
#include "SWGHoloRadarActor.h"
#include "SWGHoloShipActor.h"
#include "SWGSpaceHudWidget.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	class FSWGHoloRenderCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSWGHoloRenderCommand(TFunction<bool()> InUpdate) : UpdateFunction(MoveTemp(InUpdate)) {}
		bool Update() override { return UpdateFunction(); }
	private:
		TFunction<bool()> UpdateFunction;
	};

	/** Pixels of Rect (screen) whose brightest channel is at least Threshold. */
	int32 CountLit(const TArray<FColor>& Pixels, int32 Width, int32 Height, const FBox2D& Rect, uint8 Threshold)
	{
		int32 Lit = 0;
		for (int32 Row = FMath::Max(0, int32(Rect.Min.Y)); Row < FMath::Min(Height, int32(Rect.Max.Y)); ++Row)
		{
			for (int32 Column = FMath::Max(0, int32(Rect.Min.X)); Column < FMath::Min(Width, int32(Rect.Max.X)); ++Column)
			{
				const FColor& Pixel = Pixels[Row * Width + Column];
				if (FMath::Max3(Pixel.R, Pixel.G, Pixel.B) >= Threshold) ++Lit;
			}
		}
		return Lit;
	}
}

// Renders the radar and target holograms in a world through a scene capture, with the HUD widget composited over it, and checks they
// draw where the HUD places them. The 3D and flat captures go to .agent_tmp/stage6-captures.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceHudHoloTest, "SWG.Space.SpaceHudHolo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)
bool FSWGSpaceHudHoloTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->AddToRoot(); Game->InitializeStandalone();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
	World->AddToRoot(); Game->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(Game);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	auto Cleanup = [Game, World] { Game->Shutdown(); World->DestroyWorld(false); World->RemoveFromRoot(); Game->RemoveFromRoot(); };
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	USWGMeshGeneratorSubsystem* Meshes = Game->GetSubsystem<USWGMeshGeneratorSubsystem>();
	USWGObjectGraphSubsystem* Graph = Game->GetSubsystem<USWGObjectGraphSubsystem>();
	if (!Tre || !Meshes || !Graph) { Cleanup(); return false; }
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives available"), Tre->LoadArchives()) || !TestTrue(TEXT("the HUD can be built"), USWGSpaceHudWidget::CanBuild(Game))) { Cleanup(); return false; }
	Graph->bSpaceScene = true;

	ASWGShip* PlayerShip = World->SpawnActor<ASWGShip>();
	ASWGShip* TargetShip = World->SpawnActor<ASWGShip>();
	Graph->RegisterStaticObject(100, PlayerShip); Graph->RegisterStaticObject(200, TargetShip);
	FSWGActorSpawnArguments PlayerArgs{}; PlayerArgs.TemplateName = TEXT("object/ship/player/shared_player_yt1300.iff");
	FSWGActorSpawnArguments TargetArgs{}; TargetArgs.TemplateName = TEXT("object/ship/player/shared_player_sorosuub_space_yacht.iff");
	if (!TestTrue(TEXT("player ship spawn handler"), FSWGActorSpawnHandlerRegistry::Get().TryHandle(*PlayerShip, PlayerArgs))
		|| !TestTrue(TEXT("target ship spawn handler"), FSWGActorSpawnHandlerRegistry::Get().TryHandle(*TargetShip, TargetArgs))) { Cleanup(); return false; }
	TargetShip->SetActorLocation(FVector(180.0, -45.0, 18.0) * SWGWorldScale);
	TargetShip->ShipState.TypeName = TEXT("Sorosuub Yacht"); TargetShip->ShipState.Difficulty = TEXT("normal");
	TargetShip->ShipState.Faction = TEXT("imperial"); TargetShip->ShipState.WingName = TEXT("Dagger Wing");
	TargetShip->ShipState.FrontShield = 80.f; TargetShip->ShipState.MaxFrontShield = 100.f; TargetShip->ShipState.RearShield = 40.f; TargetShip->ShipState.MaxRearShield = 100.f;
	TargetShip->ShipState.ChassisHealth = 70.f; TargetShip->ShipState.ChassisMaxHealth = 100.f;
	TargetShip->ShipState.ArmorCurrent.Items = {TPair<uint32, float>(4, 45.f), TPair<uint32, float>(5, 50.f)};
	TargetShip->ShipState.ArmorMax.Items = {TPair<uint32, float>(4, 60.f), TPair<uint32, float>(5, 50.f)};

	const double Started = FPlatformTime::Seconds();
	AddCommand(new FSWGHoloRenderCommand([this, Game, World, Meshes, PlayerShip, TargetShip, Started, Cleanup]() mutable
	{
		Meshes->Tick(1.f / 60.f);
		World->Tick(LEVELTICK_All, 1.f / 60.f);
		auto HasRenderData = [](const AActor* Actor)
		{
			TArray<UStaticMeshComponent*> Components; Actor->GetComponents(Components);
			return Components.ContainsByPredicate([](const UStaticMeshComponent* Component)
			{
				const UStaticMesh* Mesh = Component->GetStaticMesh();
				return Component->GetVisibleFlag() && Mesh && Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0 && Mesh->GetRenderData()->LODResources[0].GetNumVertices() > 0;
			});
		};
		if (!HasRenderData(PlayerShip) || !HasRenderData(TargetShip))
		{
			if (FPlatformTime::Seconds() - Started < 120) return false;
			AddError(TEXT("ship hulls timed out")); Cleanup(); return true;
		}

		PlayerShip->SetActorHiddenInGame(true); TargetShip->SetActorHiddenInGame(true);
		const int32 Width = 1920, Height = 1080;
		FSWGSpaceHudView Camera;
		Camera.Location = FVector::ZeroVector; Camera.Rotation = FRotator::ZeroRotator; Camera.HorizontalFovDegrees = 90.f; Camera.Size = FVector2D(Width, Height);

		// The HUD with fixture data: a blip ahead-left-above, the target ship and some baselines.
		USWGSpaceHudWidget* Hud = CreateWidget<USWGSpaceHudWidget>(Game, USWGSpaceHudWidget::StaticClass());
		const TSharedRef<SWidget> HudSlate = Hud->TakeWidget();
		Hud->SetViewOverride(Camera);
		Hud->UpdateLayout(FVector2D(Width, Height));
		FSWGSpaceHudData Data;
		Data.Speed = 70.f; Data.MaxSpeed = 120.f; Data.Throttle = 0.6f;
		Data.ShipActor = PlayerShip;
		Data.RadarRange = 2048.f * SWGWorldScale;
		Data.NorthInShip = FVector(0.7, 0.7, 0.0).GetSafeNormal();
		auto AddBlip = [&Data](int64 Id, const FVector& Metres, ESWGSpaceHudBlipKind Kind, ESWGSpaceHudRelation Relation, bool bTarget)
		{
			FSWGSpaceHudBlip& Blip = Data.Blips.AddDefaulted_GetRef();
			Blip.ObjectId = Id; Blip.Relative = Metres * SWGWorldScale; Blip.Kind = Kind; Blip.Relation = Relation; Blip.bTarget = bTarget;
		};
		AddBlip(200, FVector(180, -45, 18), ESWGSpaceHudBlipKind::Ship, ESWGSpaceHudRelation::Hostile, true);
		AddBlip(301, FVector(900, 400, -300), ESWGSpaceHudBlipKind::Ship, ESWGSpaceHudRelation::Neutral, false);
		AddBlip(302, FVector(-500, -700, 450), ESWGSpaceHudBlipKind::Ship, ESWGSpaceHudRelation::Friendly, false);
		AddBlip(303, FVector(1400, -100, 200), ESWGSpaceHudBlipKind::Structure, ESWGSpaceHudRelation::Neutral, false);
		AddBlip(304, FVector(300, 900, 0), ESWGSpaceHudBlipKind::Ship, ESWGSpaceHudRelation::Hostile, false);
		Data.Vitals.FrontShield = 0.8f; Data.Vitals.RearShield = 0.4f; Data.Vitals.FrontArmor = 0.55f; Data.Vitals.RearArmor = 0.9f;
		Data.Capacitor = 640.f; Data.CapacitorMax = 1000.f; Data.Booster = 140.f; Data.BoosterMax = 200.f;
		Data.Components[0] = 0.95f; Data.Components[1] = 0.6f; Data.Components[2] = 0.35f; Data.Components[3] = 1.f; Data.Components[4] = 0.8f; Data.Components[5] = 0.15f;
		Data.Launchers = {{12, 8, 10, false}, {13, 0, 6, false}, {14, 24, 30, true}};
		Data.bHasTarget = true;
		Data.TargetLocation = TargetShip->GetActorLocation();
		Data.Target.ObjectId = 200; Data.Target.Actor = TargetShip; Data.Target.bIsShip = true; Data.Target.Relation = ESWGSpaceHudRelation::Hostile;
		Data.Target.Name = TEXT("Dagger Wing"); Data.Target.TypeName = TEXT("Sorosuub Yacht"); Data.Target.Difficulty = TEXT("normal"); Data.Target.Faction = TEXT("imperial");
		Data.Target.Vitals = FSWGSpaceHudVitals::FromBaseline(TargetShip->ShipState);
		Data.IncomingMissiles = 1;

		IConsoleVariable* FlatCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.HudFlat"));
		IConsoleVariable* DebugSlots = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.HudDebugSlots"));
		FlatCvar->Set(0, ECVF_SetByCode);
		Hud->SetData(Data);
		TestNotNull(TEXT("the radar hologram spawned"), Hud->RadarActor.Get());
		TestNotNull(TEXT("the target hologram spawned"), Hud->TargetHologram.Get());
		// The target hologram draws over the cockpit (its material has no depth test); the plain hologram material keeps its own.
		{
			int32 HoloMeshes = 0;
			TArray<UStaticMeshComponent*> HologramMeshes; Hud->TargetHologram->GetComponents(HologramMeshes);
			for (const UStaticMeshComponent* Mesh : HologramMeshes)
			{
				const UMaterialInterface* Used = Mesh->GetNumMaterials() > 0 ? Mesh->GetMaterial(0) : nullptr;
				if (!Used || !Used->GetMaterial()->GetName().StartsWith(TEXT("M_SWGHologram"))) continue;
				++HoloMeshes;
				TestEqual(TEXT("the target hologram uses the HUD hologram material"), Used->GetMaterial()->GetName(), FString(TEXT("M_SWGHologramHud")));
				TestTrue(TEXT("the target hologram material has depth test off"), Used->GetMaterial()->bDisableDepthTest);
			}
			TestTrue(TEXT("the target hologram has hologram-material meshes"), HoloMeshes > 0);
			const UMaterial* Plain = LoadObject<UMaterial>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGHologram.M_SWGHologram"));
			if (TestNotNull(TEXT("the plain hologram material exists"), Plain)) TestFalse(TEXT("the plain hologram material keeps its depth test"), Plain->bDisableDepthTest);
		}

#if WITH_EDITOR
		// Complete only the shaders still missing for the capture platform.
		TSet<FMaterialResource*> Prepared;
		for (const TCHAR* Path : {USWGSpaceHudWidget::SpriteMaterialPath, USWGSpaceHudWidget::ArcMaterialPath, USWGSpaceHudWidget::RadarMaterialPath})
		{
			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Path);
			FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
			if (Resource && !Resource->IsGameThreadShaderMapComplete()) Resource->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
		}
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
		for (int32 Frame = 0; Frame < 40; ++Frame)
		{
			Hud->SetData(Data);
			Hud->TickHud(1.f / 30.f);
			World->Tick(LEVELTICK_All, 1.f / 30.f);
		}
		// Lock to the view: the camera flies 300 m/s and turns hard every frame; the holograms must stay in their HUD slots.
		{
			auto SlotError = [&](const FSWGSpaceHudView& View, const AActor& Hologram, float CentreLift, const FVector2D& Slot)
			{
				const FVector Centre = Hologram.GetActorTransform().TransformPosition(FVector(0.f, 0.f, CentreLift));
				return FVector2D::Distance(SWGSpaceHud::Project(View, Centre).Position, Slot);
			};
			double WorstRadar = 0.0, WorstTarget = 0.0;
			FSWGSpaceHudView Flying = Camera;
			for (int32 Frame = 0; Frame < 60; ++Frame)
			{
				Flying.Location += Flying.Rotation.Vector() * 300.0 * SWGWorldScale / 30.0;
				Flying.Rotation = FRotator(20.0 * FMath::Sin(Frame * 0.4), Frame * 7.0, 15.0 * FMath::Sin(Frame * 0.3));
				PlayerShip->SetActorRotation(FRotator(5.0 * Frame, 11.0 * Frame, 3.0 * Frame));
				Hud->SetViewOverride(Flying);
				Hud->TickHud(1.f / 30.f);
				World->Tick(LEVELTICK_All, 1.f / 30.f);
				if (Frame < 3) continue;
				WorstRadar = FMath::Max(WorstRadar, double(SlotError(Flying, *Hud->RadarActor, ASWGHoloRadarActor::CentreLift, Hud->RadarSphereCentre)));
				WorstTarget = FMath::Max(WorstTarget, double(SlotError(Flying, *Hud->TargetHologram, ASWGHoloShipActor::NominalRadius * 1.1f + 5.f, Hud->TargetHoloCentre)));
			}
			AddInfo(FString::Printf(TEXT("hologram slot error while flying: radar %.3f px, target %.3f px"), WorstRadar, WorstTarget));
			TestTrue(TEXT("the radar sphere stays in its slot while the camera flies and turns"), WorstRadar < 2.0);
			TestTrue(TEXT("the target hologram stays in its slot while the camera flies and turns"), WorstTarget < 2.0);
			TestTrue(TEXT("the radar turns with the ship"), Hud->RadarActor->GetActorQuat().Equals(PlayerShip->GetActorQuat(), 1.e-3f));
			Hud->SetViewOverride(Camera);
			Hud->TickHud(1.f / 30.f);
			World->Tick(LEVELTICK_All, 1.f / 30.f);
			PlayerShip->SetActorRotation(FRotator::ZeroRotator);

			// The actors follow the live camera themselves: move the camera manager's view and tick only the world.
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			if (TestNotNull(TEXT("player controller for the live-camera lock"), Controller) && TestNotNull(TEXT("its camera manager"), Controller->PlayerCameraManager.Get()))
			{
				FSWGHoloHudAnchor LiveAnchor;
				LiveAnchor.ScreenCentre = Hud->RadarSphereCentre; LiveAnchor.RadiusPixels = Hud->RadarSpherePixels;
				LiveAnchor.NominalRadius = ASWGHoloRadarActor::NominalRadius; LiveAnchor.CentreLift = ASWGHoloRadarActor::CentreLift;
				LiveAnchor.Distance = 60.0; LiveAnchor.ViewSize = Camera.Size; LiveAnchor.Controller = Controller;
				Hud->RadarActor->SetHudAnchor(LiveAnchor);
				FMinimalViewInfo Moved;
				Moved.Location = FVector(5000.0, -2000.0, 900.0); Moved.Rotation = FRotator(15.0, 130.0, 0.0); Moved.FOV = Camera.HorizontalFovDegrees;
				Controller->PlayerCameraManager->SetCameraCachePOV(Moved);
				Hud->RadarActor->SetHudAnchor(LiveAnchor);
				FSWGSpaceHudView MovedView = Camera;
				MovedView.Location = Moved.Location; MovedView.Rotation = Moved.Rotation;
				TestTrue(TEXT("a radar anchored to the live camera places itself in that camera's view"),
					SlotError(MovedView, *Hud->RadarActor, ASWGHoloRadarActor::CentreLift, Hud->RadarSphereCentre) < 2.0);
				Moved.Location = FVector(-9000.0, 4000.0, -300.0); Moved.Rotation = FRotator(-30.0, 10.0, 0.0);
				Controller->PlayerCameraManager->SetCameraCachePOV(Moved);
				Hud->RadarActor->TickActor(1.f / 60.f, LEVELTICK_All, Hud->RadarActor->PrimaryActorTick);
				MovedView.Location = Moved.Location; MovedView.Rotation = Moved.Rotation;
				TestTrue(TEXT("the radar re-places itself from the camera manager's newest view on its own tick"),
					SlotError(MovedView, *Hud->RadarActor, ASWGHoloRadarActor::CentreLift, Hud->RadarSphereCentre) < 2.0);
				Hud->SetViewOverride(Camera);
				Hud->TickHud(1.f / 30.f);
				Controller->Destroy();
			}
		}

		World->SendAllEndOfFrameUpdates();

		AActor* CaptureActor = World->SpawnActor<AActor>();
		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(CaptureActor);
		CaptureActor->SetRootComponent(Capture); Capture->RegisterComponent();
		Capture->SetWorldLocation(Camera.Location); Capture->SetWorldRotation(Camera.Rotation);
		Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false; Capture->FOVAngle = Camera.HorizontalFovDegrees;
		Capture->ShowFlags.SetAntiAliasing(false); Capture->ShowFlags.SetMotionBlur(false); Capture->ShowFlags.SetGrain(false);
		Capture->ShowFlags.SetSeparateTranslucency(true);
		Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		Capture->PostProcessSettings.bOverride_AutoExposureMethod = true; Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true; Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
		Capture->PostProcessSettings.bOverride_AutoExposureBias = true; Capture->PostProcessSettings.AutoExposureBias = 0;
		Capture->PostProcessSettings.bOverride_BloomIntensity = true; Capture->PostProcessSettings.BloomIntensity = 0;
		UTextureRenderTarget2D* WorldTarget = NewObject<UTextureRenderTarget2D>(CaptureActor);
		WorldTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false); Capture->TextureTarget = WorldTarget;

		const FString Directory = FPaths::ProjectDir() / TEXT(".agent_tmp/stage6-captures");
		IFileManager::Get().MakeDirectory(*Directory, true);
		auto Save = [&](const TCHAR* Name, const TArray<FColor>& Pixels)
		{
			TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(Width, Height, Pixels, Png);
			TestTrue(*FString::Printf(TEXT("%s saved"), Name), FFileHelper::SaveArrayToFile(Png, *(Directory / Name)));
		};
		auto CaptureWorld = [&](TArray<FColor>& Pixels)
		{
			World->SendAllEndOfFrameUpdates(); Capture->CaptureScene();
			TestTrue(TEXT("world capture readable"), WorldTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
		};
		auto RenderWidget = [&](TArray<FColor>& Pixels)
		{
			FWidgetRenderer Renderer(false);
			Renderer.SetIsPrepassNeeded(true);
			UTextureRenderTarget2D* Target = Renderer.DrawWidget(HudSlate, FVector2D(Width, Height));
			if (Target && Target->GameThread_GetRenderTargetResource()) Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
		};
		auto Composite = [&](const TArray<FColor>& Back, const TArray<FColor>& Front)
		{
			TArray<FColor> Result = Back;
			for (int32 Index = 0; Index < Result.Num() && Index < Front.Num(); ++Index)
			{
				Result[Index].R = uint8(FMath::Min(255.f, Result[Index].R + Front[Index].R));
				Result[Index].G = uint8(FMath::Min(255.f, Result[Index].G + Front[Index].G));
				Result[Index].B = uint8(FMath::Min(255.f, Result[Index].B + Front[Index].B));
			}
			return Result;
		};

		// 3D mode: the holograms must draw in their regions.
		Hud->StartIntro(); Hud->TickHud(3.f);
		TArray<FColor> World3D, Widget3D;
		CaptureWorld(World3D);
		RenderWidget(Widget3D);
		const FBox2D RadarRegion(Hud->RadarSphereCentre - FVector2D(Hud->RadarSpherePixels), Hud->RadarSphereCentre + FVector2D(Hud->RadarSpherePixels));
		const FBox2D TargetRegion(Hud->TargetHoloCentre - FVector2D(Hud->TargetHoloPixels), Hud->TargetHoloCentre + FVector2D(Hud->TargetHoloPixels));
		const int32 RadarLit = CountLit(World3D, Width, Height, RadarRegion, 24);
		const int32 TargetLit = CountLit(World3D, Width, Height, TargetRegion, 24);
		AddInfo(FString::Printf(TEXT("3D lit pixels: radar %d of %d, target %d of %d; radar region (%.0f,%.0f)-(%.0f,%.0f)"), RadarLit, int32(RadarRegion.GetArea()), TargetLit,
			int32(TargetRegion.GetArea()), RadarRegion.Min.X, RadarRegion.Min.Y, RadarRegion.Max.X, RadarRegion.Max.Y));
		TestTrue(TEXT("the radar hologram draws inside its screen region"), RadarLit > 400);
		TestTrue(TEXT("the target hologram draws inside its screen region"), TargetLit > 400);
		const FBox2D Elsewhere(FVector2D(Width * 0.3, Height * 0.1), FVector2D(Width * 0.7, Height * 0.45));
		TestTrue(TEXT("nothing draws in the empty middle of the screen"), CountLit(World3D, Width, Height, Elsewhere, 24) < 50);
		Save(TEXT("holo-3d-world.png"), World3D);
		Save(TEXT("holo-3d-hud.png"), Composite(World3D, Widget3D));
		// CPU cost of the HUD's per-frame work (data apply, layout checks, gauges, hologram placement and springs), 3D mode.
		{
			constexpr int32 Warmup = 30, Samples = 600;
			double Total = 0, Worst = 0;
			for (int32 Index = 0; Index < Warmup + Samples; ++Index)
			{
				const double Start = FPlatformTime::Seconds();
				Hud->SetData(Data); Hud->TickHud(1.f / 60.f);
				const double Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
				if (Index < Warmup) continue;
				Total += Milliseconds; Worst = FMath::Max(Worst, Milliseconds);
			}
			AddInfo(FString::Printf(TEXT("CPU HUD SetData + TickHud (3D, full data): avg %.4f ms, worst %.4f ms"), Total / Samples, Worst));
			TestTrue(TEXT("the HUD update stays under 2 ms on average"), Total / Samples < 2.0);
			FFileHelper::SaveStringToFile(FString::Printf(TEXT("HUD SetData + TickHud (3D, full data), Development editor build, 600 frames after 30 warm-up: avg %.4f ms  worst %.4f ms\n"), Total / Samples, Worst),
				*(FPaths::ProjectDir() / TEXT(".agent_tmp/stage7-captures/cpu-hud.txt")));
		}
		DebugSlots->Set(1, ECVF_SetByCode);
		Hud->UpdateLayout(FVector2D(Width, Height));
		Hud->TickHud(3.f);
		TArray<FColor> WidgetSlots;
		RenderWidget(WidgetSlots);
		Save(TEXT("holo-3d-hud-slots.png"), Composite(World3D, WidgetSlots));
		DebugSlots->Set(0, ECVF_SetByCode);
		Hud->UpdateLayout(FVector2D(Width, Height));

		// Flat mode: the holograms are gone and the world capture is empty there.
		FlatCvar->Set(1, ECVF_SetByCode);
		Hud->SetData(Data);
		Hud->TickHud(0.016f);
		World->Tick(LEVELTICK_All, 1.f / 30.f);
		TestNull(TEXT("flat mode removes the radar hologram"), Hud->RadarActor.Get());
		TArray<FColor> WorldFlat, WidgetFlat;
		CaptureWorld(WorldFlat);
		RenderWidget(WidgetFlat);
		TestTrue(TEXT("flat mode leaves the radar region of the world empty"), CountLit(WorldFlat, Width, Height, RadarRegion, 24) < 50);
		Save(TEXT("holo-flat-hud.png"), Composite(WorldFlat, WidgetFlat));
		FlatCvar->Set(0, ECVF_SetByCode);
		Cleanup();
		return true;
	}));
	return !HasAnyErrors();
}
#endif
