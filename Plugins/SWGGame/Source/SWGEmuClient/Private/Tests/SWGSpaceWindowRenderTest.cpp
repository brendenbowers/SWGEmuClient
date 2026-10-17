#include "Misc/AutomationTest.h"
#include "Common/SWGWorldScale.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Objects/Player/SWGPlayer.h"
#include "Components/SWGTangibleComponent.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"
#include "Camera/CameraTypes.h"
#include "GameFramework/WorldSettings.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "HAL/IConsoleManager.h"
#include "Async/TaskGraphInterfaces.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "SceneInterface.h"
#include "MaterialShared.h"
#include "ImageUtils.h"
#include "TextureResource.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#endif
#include "Objects/World/SWGShip.h"
#include "Objects/World/SWGCell.h"
#include "Objects/World/SWGDoor.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Components/SWGSpaceNebulaComponent.h"
#include "Components/SWGSpaceDustComponent.h"
#include "Components/SWGSpacePlanetComponent.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGDoorStyleRow.h"
#include "TRE/SWGShaderReader.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	class FSWGWindowRenderCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSWGWindowRenderCommand(TFunction<bool()> InUpdate) : UpdateFunction(MoveTemp(InUpdate)) {}
		bool Update() override { return UpdateFunction(); }
	private:
		TFunction<bool()> UpdateFunction;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceWindowRenderTest, "SWG.Space.YachtWindowRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI)

bool FSWGSpaceWindowRenderTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->AddToRoot(); Game->InitializeStandalone();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
	World->AddToRoot(); Game->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(Game);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	TestTrue(TEXT("door fixture world dispatches real overlap events"), World->HasBegunPlay());
	auto Cleanup = [Game, World] { Game->Shutdown(); World->DestroyWorld(false); World->RemoveFromRoot(); Game->RemoveFromRoot(); };
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	USWGMeshGeneratorSubsystem* Meshes = Game->GetSubsystem<USWGMeshGeneratorSubsystem>();
	USWGObjectGraphSubsystem* Graph = Game->GetSubsystem<USWGObjectGraphSubsystem>();
	USWGTerrainSubsystem* Terrain = Game->GetSubsystem<USWGTerrainSubsystem>();
	if (!Tre || !Meshes || !Graph || !Terrain) { Cleanup(); return false; }
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives available"), Tre->LoadArchives())) { Cleanup(); return false; }
	FSWGDataTableData DoorStyles;
	if (!TestTrue(TEXT("door style data loads"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/appearance/door_style.iff")), DoorStyles))
		|| !TestNotNull(TEXT("door table initializes as in client startup"), SWGDoorStyle::BuildDataTable(DoorStyles))) { Cleanup(); return false; }
	Terrain->BeginLoadTerrain(TEXT("terrain/space_tatooine.trn"), FVector::ZeroVector);
	ASWGShip* Ship = World->SpawnActor<ASWGShip>();
	ASWGCell* Room = World->SpawnActor<ASWGCell>();
	Graph->RegisterStaticObject(100, Ship); Graph->RegisterStaticObject(101, Room, 100);
	Graph->LocalPlayerObjectId = 103; Graph->ContainerByObjectId.Add(103, 101); Graph->bSpaceScene = true;
	Graph->SpaceObjectPoses.Add(100, FTransform::Identity);
	FSWGActorSpawnArguments Args{}; Args.TemplateName = TEXT("object/ship/player/shared_player_sorosuub_space_yacht.iff");
	if (!TestTrue(TEXT("actual yacht spawn handler accepts template"), FSWGActorSpawnHandlerRegistry::Get().TryHandle(*Ship, Args))) { Cleanup(); return false; }
	ASWGShip* SelectedShip = World->SpawnActor<ASWGShip>();
	if (!TestNotNull(TEXT("selected YT1300 fixture spawns"), SelectedShip)) { Cleanup(); return false; }
	Graph->RegisterStaticObject(201, SelectedShip);
	FSWGActorSpawnArguments SelectedArgs{}; SelectedArgs.TemplateName = TEXT("object/ship/player/shared_player_yt1300.iff");
	if (!TestTrue(TEXT("actual YT1300 spawn handler accepts template"), FSWGActorSpawnHandlerRegistry::Get().TryHandle(*SelectedShip, SelectedArgs))) { Cleanup(); return false; }
	SelectedShip->SetActorHiddenInGame(true); SelectedShip->SetActorEnableCollision(false);
	const FSWGPobCell* Observation = Ship->PortalData.Cells.FindByPredicate([](const FSWGPobCell& Cell) { return Cell.CellIndex == 6; });
	if (!Observation) { AddError(TEXT("yacht observation room absent")); Cleanup(); return false; }
	const FSWGPobPortalRef* Window = Observation->Portals.FindByPredicate([](const FSWGPobPortalRef& Portal) { return Portal.ConnectingCellIndex == 0 && !Portal.bIsPassable; });
	if (!Window || Window->OpeningIndices.Num() < 3) { AddError(TEXT("observation window has no indexed opening")); Cleanup(); return false; }
	FVector Center = FVector::ZeroVector, OpeningNormal = FVector::ZeroVector;
	double LargestArea = 0;
	for (int32 Index = 0; Index+2 < Window->OpeningIndices.Num(); Index += 3)
	{
		const FVector A = Window->OpeningVertices[Window->OpeningIndices[Index]];
		const FVector B = Window->OpeningVertices[Window->OpeningIndices[Index+1]];
		const FVector C = Window->OpeningVertices[Window->OpeningIndices[Index+2]];
		const FVector Normal = FVector::CrossProduct(B-A, C-A);
		if (Normal.SizeSquared() > LargestArea)
		{
			LargestArea = Normal.SizeSquared(); Center = (A+B+C)/3; OpeningNormal = Normal.GetSafeNormal();
		}
	}
	if (OpeningNormal.IsNearlyZero()) { AddError(TEXT("observation opening has no nondegenerate triangle")); Cleanup(); return false; }
	FString RoomMeshPath;
	FSWGMeshData RoomMesh;
	if (Meshes->ResolveLodMeshPath(Observation->MeshPath, RoomMeshPath) && FSWGMeshReader::ReadStaticMesh(Tre->CreateIffReader(RoomMeshPath), RoomMesh))
	{
		for (const FSWGMeshSubmesh& Submesh : RoomMesh.Submeshes)
		{
			FSWGShaderData Shader;
			if (FSWGShaderReader::ReadShader(Tre->CreateIffReader(Submesh.ShaderName), Shader))
			{
				AddInfo(FString::Printf(TEXT("room shader=%s effect=%s textures=%d"), *Submesh.ShaderName, *Shader.EffectName, Shader.Textures.Num()));
				if (Submesh.ShaderName.Contains(TEXT("collide")) || Submesh.ShaderName.Contains(TEXT("glass")))
				{
					FSWGEffectData Effect;
					if (FSWGShaderReader::ReadEffect(Tre->CreateIffReader(Shader.bHasInlineEffect ? Submesh.ShaderName : Shader.EffectName), Effect)
						&& !Effect.Implementations.IsEmpty())
						for (const FSWGEffectPass& Pass : Effect.Implementations[0].Passes)
							AddInfo(FString::Printf(TEXT("window effect pass: shader=%s blend=%d depthWrite=%d colorMask=%d"), *Submesh.ShaderName, Pass.bAlphaBlend, Pass.bDepthWrite, Pass.ColorWriteMask));
					for (const FSWGShaderTexture& Texture : Shader.Textures)
						AddInfo(FString::Printf(TEXT("window texture: shader=%s slot=%s path=%s"), *Submesh.ShaderName, *Texture.Tag, *Texture.VirtualPath));
				}
			}
		}
	}
	FSWGCellSpawnHandler::FinishCell(Room, Ship, 6, Tre, Meshes, true);
	const double Started = FPlatformTime::Seconds();
	AddCommand(new FSWGWindowRenderCommand([this, Game, World, Ship, SelectedShip, Room, Meshes, Graph, Terrain, Center, OpeningNormal, Started, Cleanup]() mutable
	{
		Meshes->Tick(1.f/60.f);
		World->Tick(LEVELTICK_All, 1.f/60.f);
		SelectedShip->SetActorHiddenInGame(true);
		TArray<UStaticMeshComponent*> HullComponents; Ship->GetComponents(HullComponents);
		TArray<UStaticMeshComponent*> SelectedHullComponents; SelectedShip->GetComponents(SelectedHullComponents);
		auto HasRenderData = [](const UStaticMeshComponent* Component)
		{
			const UStaticMesh* Mesh = Component->GetStaticMesh();
			return Component->GetVisibleFlag() && Mesh && Mesh->GetRenderData()
				&& Mesh->GetRenderData()->LODResources.Num() > 0 && Mesh->GetRenderData()->LODResources[0].GetNumVertices() > 0;
		};
		const bool bHullReady = HullComponents.ContainsByPredicate(HasRenderData) && SelectedHullComponents.ContainsByPredicate(HasRenderData);
		const bool bDoorsReady = !Ship->Doors.IsEmpty() && !Ship->Doors.ContainsByPredicate([](const ASWGDoor* Door)
		{
			return !Door || !Cast<UStaticMeshComponent>(Door->GetRootComponent()) || Door->OpenOffset.IsNearlyZero();
		});
		if (!Room->bCollisionReady || !bHullReady || !bDoorsReady)
		{
			if (FPlatformTime::Seconds()-Started < 120) return false;
			AddError(TEXT("yacht visible hull, room collision or authored door leaves timed out")); Cleanup(); return true;
		}
		TMap<int32, TArray<ASWGDoor*>> DoorLeaves;
		for (ASWGDoor* Door : Ship->Doors) DoorLeaves.FindOrAdd(Door->PortalNumber).Add(Door);
		for (ASWGDoor* Door : Ship->Doors)
			TestTrue(TEXT("door leaves receive both room and exterior boundary lighting"), CastChecked<UPrimitiveComponent>(Door->GetRootComponent())->LightingChannels.bChannel0
				&& CastChecked<UPrimitiveComponent>(Door->GetRootComponent())->LightingChannels.bChannel1);
		int32 DoubleDoors = 0;
		for (const auto& Pair : DoorLeaves)
		{
			if (Pair.Value.Num() != 2) continue;
			++DoubleDoors;
			ASWGDoor* First = Pair.Value[0]; ASWGDoor* Second = Pair.Value[1];
			const FTransform FirstClosed = First->GetActorTransform(), SecondClosed = Second->GetActorTransform();
			TestTrue(TEXT("double door leaves share the authored hinge origin"), FirstClosed.GetLocation().Equals(SecondClosed.GetLocation(), .01));
			TestTrue(TEXT("retail doorFlip2 rotates the second leaf"), (FTransform(FRotator(0,180,0)) * FirstClosed).GetRotation().Equals(SecondClosed.GetRotation(), .001));
			TestTrue(TEXT("both door sensors share a stationary portal center"), First->TriggerVolume->GetComponentLocation().Equals(Second->TriggerVolume->GetComponentLocation(), .01));
			APawn* Walker = World->SpawnActor<APawn>();
			if (!Walker) { AddError(TEXT("door walker fixture failed to spawn")); Cleanup(); return true; }
			UCapsuleComponent* WalkerCapsule = NewObject<UCapsuleComponent>(Walker);
			Walker->SetRootComponent(WalkerCapsule); WalkerCapsule->SetCapsuleSize(34,88);
			WalkerCapsule->SetWorldLocation(FVector(200000));
			WalkerCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly); WalkerCapsule->SetCollisionObjectType(ECC_Pawn);
			WalkerCapsule->SetCollisionResponseToAllChannels(ECR_Ignore); WalkerCapsule->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Overlap);
			WalkerCapsule->SetGenerateOverlapEvents(true); WalkerCapsule->RegisterComponent();
			if (!Walker->HasActorBegunPlay()) Walker->DispatchBeginPlay();
			if (!First->HasActorBegunPlay()) First->DispatchBeginPlay();
			if (!Second->HasActorBegunPlay()) Second->DispatchBeginPlay();
			const FVector SensorCenter = First->TriggerVolume->GetComponentLocation();
			Walker->SetActorLocation(SensorCenter); WalkerCapsule->UpdateOverlaps();
			First->TriggerVolume->UpdateOverlaps(); Second->TriggerVolume->UpdateOverlaps();
			AddInfo(FString::Printf(TEXT("door overlap fixture: capsule registered=%d collision=%d door registered=%d collision=%d events=%d overlaps=%d"),
				WalkerCapsule->IsRegistered(), int32(WalkerCapsule->GetCollisionEnabled()), First->TriggerVolume->IsRegistered(),
				int32(First->TriggerVolume->GetCollisionEnabled()), First->TriggerVolume->GetGenerateOverlapEvents(), First->TriggerVolume->IsOverlappingActor(Walker)));
			TestTrue(TEXT("walking into the real doorway opens both leaves"), First->bIsOpen && Second->bIsOpen);
			First->Open(); Second->Open();
			First->Tick(First->OpenTime*.5f); Second->Tick(Second->OpenTime*.5f);
			const FVector FirstSlide = First->GetActorLocation()-FirstClosed.GetLocation();
			const FVector SecondSlide = Second->GetActorLocation()-SecondClosed.GetLocation();
			TestTrue(TEXT("authored leaves open in opposite directions"), !FirstSlide.IsNearlyZero() && FirstSlide.Equals(-SecondSlide, .01));
			TestTrue(TEXT("open leaf motion cannot move either proximity sensor"), First->TriggerVolume->GetComponentLocation().Equals(SensorCenter,.01)
				&& Second->TriggerVolume->GetComponentLocation().Equals(SensorCenter,.01));
			const FTransform OpenBeforeFlight = First->GetActorTransform();
			Ship->SpacePose = FTransform(FRotator(20,30,40), FVector(10000,20000,30000)); Graph->RefreshSpaceFrame();
			TestTrue(TEXT("moving ship keeps open door in its fixed interior frame"), First->GetActorTransform().Equals(OpenBeforeFlight, .01));
			Walker->SetActorLocation(FVector(200000)); WalkerCapsule->UpdateOverlaps();
			First->TriggerVolume->UpdateOverlaps(); Second->TriggerVolume->UpdateOverlaps();
			TestTrue(TEXT("walking away closes both leaves"), !First->bIsOpen && !Second->bIsOpen);
			Walker->Destroy();
			First->Tick(First->CloseTime); Second->Tick(Second->CloseTime);
			TestTrue(TEXT("both door leaves return to their authored closed transforms"), First->GetActorTransform().Equals(FirstClosed, .01) && Second->GetActorTransform().Equals(SecondClosed, .01));
		}
		TestTrue(TEXT("real yacht room exercises an authored double door"), DoubleDoors > 0);
		Ship->SpacePose = FTransform::Identity; Graph->RefreshSpaceFrame();
		Ship->SetLitRoom(Room);
		FVector BoundsCenter, BoundsExtent; Room->GetActorBounds(false, BoundsCenter, BoundsExtent);
		FVector Outward = OpeningNormal;
		if (FVector::DotProduct(Outward, Center-BoundsCenter) < 0) Outward *= -1;
		const FVector Horizontal = FVector(Outward.X, Outward.Y, 0).GetSafeNormal();
		if (!Horizontal.IsNearlyZero()) Outward = Horizontal;
		AActor* CaptureActor = World->SpawnActor<AActor>();
		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(CaptureActor);
		CaptureActor->SetRootComponent(Capture); Capture->RegisterComponent();
		Capture->SetWorldLocation(Center-Outward*300); Capture->SetWorldRotation(Outward.Rotation());
		Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false; Capture->FOVAngle = 80;
		Capture->ShowFlags.SetHeterogeneousVolumes(true);
		Capture->ShowFlags.SetAntiAliasing(false); Capture->ShowFlags.SetMotionBlur(false); Capture->ShowFlags.SetGrain(false);
		Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		// Scene captures disable the after-DOF translucency pass by default; the normal game camera includes it.
		Capture->ShowFlags.SetSeparateTranslucency(true);
		Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
		Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
		Capture->PostProcessSettings.bOverride_AutoExposureBias = true; Capture->PostProcessSettings.AutoExposureBias = 0;
		Capture->PostProcessSettings.bOverride_BloomIntensity = true; Capture->PostProcessSettings.BloomIntensity = 0;
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(CaptureActor);
		Target->InitCustomFormat(512, 512, PF_B8G8R8A8, false); Capture->TextureTarget = Target;
		AActor* Marker = World->SpawnActor<AActor>();
		UStaticMeshComponent* MarkerMesh = NewObject<UStaticMeshComponent>(Marker);
		MarkerMesh->bDisallowNanite = true;
		Marker->SetRootComponent(MarkerMesh);
		MarkerMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
		TestTrue(TEXT("marker sphere mesh loaded"), MarkerMesh->GetStaticMesh() != nullptr);
		MarkerMesh->RegisterComponent(); Marker->SetActorScale3D(FVector(5));
		Marker->SetActorLocation(Center+Outward*2000);
		UMaterial* ReferenceMaterial = NewObject<UMaterial>(Marker);
#if WITH_EDITOR
		ReferenceMaterial->SetShadingModel(MSM_Unlit);
		UMaterialExpressionConstant3Vector* Color = NewObject<UMaterialExpressionConstant3Vector>(ReferenceMaterial);
		Color->Constant = FLinearColor::Red;
		ReferenceMaterial->GetExpressionCollection().AddExpression(Color);
		ReferenceMaterial->GetEditorOnlyData()->EmissiveColor.Expression = Color;
		ReferenceMaterial->PostEditChange();
#endif
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ReferenceMaterial, Marker);
		if (!Material) { AddError(TEXT("space marker material unavailable")); Cleanup(); return true; }
		TestFalse(TEXT("external marker respects depth occlusion"), Material->GetMaterial()->bDisableDepthTest);
		MarkerMesh->SetMaterial(0, Material);
#if WITH_EDITOR
		// This standalone world needs shaders for its actual capture platform, including materials loaded asynchronously.
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
					// Recaching a complete shader map leaves it incomplete; only compile what is missing.
					if (UsedResource && !PreparedResources.Contains(UsedResource) && !UsedResource->IsGameThreadShaderMapComplete())
					{
						UsedResource->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
						PreparedResources.Add(UsedResource);
					}
				}
			}
		}
		FAssetCompilingManager::Get().FinishAllCompilation();
#endif
		Graph->RegisterStaticObject(200, Marker); Graph->SpaceObjectPoses.Add(200, Marker->GetActorTransform());
		World->Tick(LEVELTICK_All, 1.f/60.f);
		World->SendAllEndOfFrameUpdates();
		TestTrue(TEXT("marker render state created"), MarkerMesh->IsRenderStateCreated());
		FMaterialResource* Resource = ReferenceMaterial->GetMaterialResource(World->Scene->GetShaderPlatform());
		TestTrue(TEXT("reference shader ready for capture platform"), Resource && Resource->IsGameThreadShaderMapComplete());
		AddInfo(FString::Printf(TEXT("reference material platform=%d resource=%d complete=%d compiling=%d materials=%d lightingOnly=%d diffuseOverride=%d"),
			int32(World->Scene->GetShaderPlatform()), Resource != nullptr, Resource && Resource->IsGameThreadShaderMapComplete(), ReferenceMaterial->IsCompiling(),
			Capture->ShowFlags.Materials, Capture->ShowFlags.LightingOnlyOverride, Capture->ShowFlags.OverrideDiffuseAndSpecular));
		AddInfo(FString::Printf(TEXT("marker=%s bounds=%s visible=%d hidden=%d registered=%d cameraForward=%s translucency=%d separate=%d blend=%d"),
			*Marker->GetActorLocation().ToString(), *MarkerMesh->Bounds.ToString(), MarkerMesh->GetVisibleFlag(), Marker->IsHidden(), MarkerMesh->IsRegistered(),
			*Capture->GetForwardVector().ToString(), Capture->ShowFlags.Translucency, Capture->ShowFlags.SeparateTranslucency, int32(Material->GetBlendMode())));
		const FTransform RoomBefore = Room->GetActorTransform();
		TArray<FColor> Control, Before, WalkView, RollView, After;
		UStaticMeshComponent* StarDome = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UStaticMeshComponent*> Components; It->GetComponents(Components);
			for (UStaticMeshComponent* Component : Components)
				if (Component->GetFName() == FName(TEXT("SWGStarDome"))) StarDome = Component;
		}
		TestNotNull(TEXT("space star dome available for rendered validation"), StarDome);
		if (StarDome) TestEqual(TEXT("stars composite before gas volumes"),StarDome->GetMaterial(0)->GetMaterial()->TranslucencyPass.GetValue(),MTP_BeforeDOF);
		UMaterial* SkyBackground = LoadObject<UMaterial>(nullptr,TEXT("/Game/SWGEmu/Materials/M_SWGSkySpriteAlpha.M_SWGSkySpriteAlpha"));
		if (TestNotNull(TEXT("space sky background material available"),SkyBackground))
			TestEqual(TEXT("sky background composites before gas volumes"),SkyBackground->TranslucencyPass.GetValue(),MTP_BeforeDOF);
		TArray<FColor> StarsBefore, StarsRolled;
		auto CapturePixels = [&](const TCHAR* Name, TArray<FColor>& Pixels)
		{
			Game->GetSubsystem<USWGTerrainSubsystem>()->Tick(1.f/60.f);
			World->SendAllEndOfFrameUpdates(); Capture->CaptureScene();
			if (!TestTrue(TEXT("render target pixels readable"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return;
			TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(512, 512, Pixels, Png);
			const FString Directory = FPaths::ProjectSavedDir()/TEXT("Automation/SpaceWindows");
			IFileManager::Get().MakeDirectory(*Directory, true);
			TestTrue(TEXT("window evidence saved"), FFileHelper::SaveArrayToFile(Png, *(Directory/Name)));
		};
		auto CaptureStars = [&](const TCHAR* Name, TArray<FColor>& Pixels)
		{
			if (!StarDome) return;
			Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			Capture->ShowOnlyComponent(StarDome);
			CapturePixels(Name, Pixels);
			Capture->ShowOnlyComponents.Reset();
			Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
		};
		// Compare the old conversion against the fixed one in the actual yacht room,
		// at identical manual exposure, before restoring the window fixture's lights.
		const FTransform WindowView = Capture->GetComponentTransform();
		Capture->SetWorldRotation((-Capture->GetForwardVector()).Rotation());
		Room->SetRoomLightsEnabled(true);
		TArray<float> FixedIntensities;
		for (ULightComponent* Light : Room->RoomLights) FixedIntensities.Add(Light->Intensity);
		const auto& AuthoredLights = Ship->PortalData.Cells[6].Lights;
		if (TestEqual(TEXT("room retains all authored lights"), Room->RoomLights.Num(), AuthoredLights.Num()))
		{
			for (int32 Index = 0; Index < AuthoredLights.Num(); ++Index)
			{
				const FSWGPobLight& Authored = AuthoredLights[Index];
				if (Authored.Type != ESWGPobLightType::Point) continue;
				const float Denominator = Authored.ConstantAttenuation + Authored.LinearAttenuation*2 + Authored.QuadraticAttenuation*4;
				Room->RoomLights[Index]->SetIntensity(FixedIntensities[Index] * Denominator / (2*FMath::Max(Authored.LinearAttenuation, KINDA_SMALL_NUMBER)));
			}
			TArray<FColor> OldLighting, FixedLighting;
			CapturePixels(TEXT("yacht-interior-old-attenuation.png"), OldLighting);
			for (int32 Index = 0; Index < FixedIntensities.Num(); ++Index) Room->RoomLights[Index]->SetIntensity(FixedIntensities[Index]);
			CapturePixels(TEXT("yacht-interior-fixed-attenuation.png"), FixedLighting);
			auto ClippedPixels = [](const TArray<FColor>& Pixels)
			{
				int32 Count = 0;
				for (const FColor& Pixel : Pixels) Count += Pixel.R >= 250 && Pixel.G >= 250 && Pixel.B >= 250;
				return Count;
			};
			const int32 OldClipped = ClippedPixels(OldLighting), FixedClipped = ClippedPixels(FixedLighting);
			AddInfo(FString::Printf(TEXT("yacht interior clipped pixels: old=%d fixed=%d"), OldClipped, FixedClipped));
			TestTrue(TEXT("authored quadratic falloff removes interior overexposure"), OldClipped > FixedClipped + 100);
		}
		Room->SetRoomLightsEnabled(false); Capture->SetWorldTransform(WindowView);
		for (const auto& Pair : DoorLeaves)
		{
			if (Pair.Value.Num() != 2) continue;
			ASWGDoor* First = Pair.Value[0]; ASWGDoor* Second = Pair.Value[1];
			UStaticMeshComponent* FirstMesh = CastChecked<UStaticMeshComponent>(First->GetRootComponent());
			UStaticMeshComponent* SecondMesh = CastChecked<UStaticMeshComponent>(Second->GetRootComponent());
			const FVector Extent = FirstMesh->GetStaticMesh()->GetBoundingBox().GetExtent();
			const FVector LocalNormal = Extent.X < Extent.Y && Extent.X < Extent.Z ? FVector::ForwardVector
				: Extent.Y < Extent.Z ? FVector::RightVector : FVector::UpVector;
			const FVector Normal = First->GetActorTransform().TransformVectorNoScale(LocalNormal).GetSafeNormal();
			const FVector DoorCenter = (FirstMesh->Bounds.Origin + SecondMesh->Bounds.Origin)*.5;
			const FTransform WindowCamera = Capture->GetComponentTransform();
			Capture->SetWorldLocation(DoorCenter + Normal*350); Capture->SetWorldRotation((-Normal).Rotation());
			Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			Capture->ShowOnlyComponent(FirstMesh); Capture->ShowOnlyComponent(SecondMesh);
			TArray<FColor> ClosedPixels, OpenPixels;
			CapturePixels(TEXT("yacht-double-door-closed.png"), ClosedPixels);
			First->Open(); Second->Open(); First->Tick(First->OpenTime); Second->Tick(Second->OpenTime);
			CapturePixels(TEXT("yacht-double-door-open.png"), OpenPixels);
			int32 Changed = 0;
			for (int32 Index = 0; Index < FMath::Min(ClosedPixels.Num(), OpenPixels.Num()); ++Index)
				Changed += FMath::Abs(int32(ClosedPixels[Index].R)-int32(OpenPixels[Index].R)) > 20
					|| FMath::Abs(int32(ClosedPixels[Index].G)-int32(OpenPixels[Index].G)) > 20
					|| FMath::Abs(int32(ClosedPixels[Index].B)-int32(OpenPixels[Index].B)) > 20;
			TestTrue(TEXT("real double door animation changes rendered pixels"), Changed > 100);
			First->Close(); Second->Close(); First->Tick(First->CloseTime); Second->Tick(Second->CloseTime);
			Capture->ShowOnlyComponents.Reset(); Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
			Capture->SetWorldTransform(WindowCamera);
			break;
		}
		if (TestTrue(TEXT("authored gas nebula available for rendering"),!Terrain->SpaceNebula->Nebulas.IsEmpty()))
		{
			const auto Nebula = Terrain->SpaceNebula->Nebulas[0];
			UMaterial* GasMaterial = Nebula.Volume->GetMaterial(0)->GetMaterial();
			AddInfo(FString::Printf(TEXT("gas material=%s domain=%d shading=%d usage=%d bounds=%s scale=%s"),
				*GasMaterial->GetName(),int32(GasMaterial->MaterialDomain),int32(GasMaterial->GetShadingModels().GetFirstShadingModel()),
				GasMaterial->GetUsageByFlag(MATUSAGE_HeterogeneousVolumes),*Nebula.Volume->Bounds.ToString(),*Nebula.Volume->GetComponentScale().ToString()));
			const FTransform OriginalPose = Ship->SpacePose, OriginalCamera = Capture->GetComponentTransform();
			const FVector NativeDirection = Nebula.SpacePosition.GetSafeNormal();
			auto AimNebula = [&](const FVector& Direction)
			{
				Ship->SpacePose = FTransform(FQuat::FindBetweenNormals(Direction,NativeDirection),
					Nebula.SpacePosition-NativeDirection*Nebula.Radius*3.5);
				Graph->RefreshSpaceFrame(); Terrain->Tick(1.f/60.f);
				TestTrue(TEXT("gas nebula follows the absolute space frame"),Nebula.Volume->GetComponentLocation().Equals(
					Graph->GetSpaceFrame().InverseTransformPosition(Nebula.SpacePosition),1));
			};
			auto GasVisible = [&](bool bVisible)
			{
				for (const auto& Gas : Terrain->SpaceNebula->Nebulas) Gas.Volume->SetVisibility(bVisible);
			};
			auto ChangedPixels = [](const TArray<FColor>& A,const TArray<FColor>& B,const TArray<FLinearColor>& Depth,bool bOpaque)
			{
				int32 Changed = 0;
				for (int32 Index = 0; Index < FMath::Min(A.Num(),B.Num()); ++Index)
				{
					if (!Depth.IsValidIndex(Index) || (Depth[Index].R > 0 && Depth[Index].R < 10000) != bOpaque) continue;
					// LDR readback dithers dark pixels by up to 13 channel values; require a visible change.
					Changed += FMath::Abs(int32(A[Index].R)-int32(B[Index].R)) > 20
						|| FMath::Abs(int32(A[Index].G)-int32(B[Index].G)) > 20
						|| FMath::Abs(int32(A[Index].B)-int32(B[Index].B)) > 20;
				}
				return Changed;
			};
			for (bool bWindow : {true,false})
			{
				const FVector Direction = bWindow ? Outward : FVector::DownVector;
				AimNebula(Direction); Capture->SetWorldRotation(Direction.Rotation());
				Capture->FOVAngle = bWindow ? 80 : 3;
				GasVisible(false); Nebula.Volume->SetVisibility(true);
				Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
				Capture->ShowOnlyComponents.Reset(); Capture->ShowOnlyComponent(Nebula.Volume);
				TArray<FColor> GasOnly, WithGas, WithoutGas;
				CapturePixels(bWindow ? TEXT("nebula-window-control.png") : TEXT("nebula-wall-control.png"),GasOnly);
				int32 Visible = 0;
				for (const FColor& Pixel : GasOnly) Visible += Pixel.R > 20 || Pixel.G > 20 || Pixel.B > 20;
				TestTrue(TEXT("native gas volume actually renders at authored kilometre scale"),Visible > 100);

				Capture->ShowOnlyComponents.Reset(); Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
				// Aim the occlusion probe at the solid interior floor, and verify its actual scene depth.
				UTextureRenderTarget2D* DepthTarget = NewObject<UTextureRenderTarget2D>(CaptureActor);
				DepthTarget->InitCustomFormat(512,512,PF_A32B32G32R32F,true);
				Capture->TextureTarget = DepthTarget; Capture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
				World->SendAllEndOfFrameUpdates(); Capture->CaptureScene();
				TArray<FLinearColor> Depth;
				TestTrue(TEXT("opaque hull depth is readable"),DepthTarget->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Depth));
				int32 OpaquePixels = 0;
				for (const FLinearColor& Pixel : Depth) OpaquePixels += Pixel.R > 0 && Pixel.R < 10000;
				TestTrue(TEXT("occlusion fixture contains actual opaque hull"),OpaquePixels > 1000);
				if (!bWindow) TestTrue(TEXT("floor view contains solid hull rather than windows"),OpaquePixels > Depth.Num()*.99);
				Capture->TextureTarget = Target; Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
				CapturePixels(bWindow ? TEXT("nebula-through-window.png") : TEXT("nebula-behind-wall.png"),WithGas);
				GasVisible(false);
				CapturePixels(bWindow ? TEXT("nebula-window-hidden.png") : TEXT("nebula-wall-hidden.png"),WithoutGas);
				const int32 Changed = ChangedPixels(WithGas,WithoutGas,Depth,false);
				const int32 HullChanged = ChangedPixels(WithGas,WithoutGas,Depth,true);
				AddInfo(FString::Printf(TEXT("nebula %s: control=%d opening changed=%d opaque changed=%d/%d"),bWindow ? TEXT("window") : TEXT("wall"),Visible,Changed,HullChanged,OpaquePixels));
				if (bWindow) TestTrue(TEXT("gas cloud is visible through the actual yacht window"),Changed > 50);
				else TestTrue(TEXT("opaque yacht wall occludes the gas cloud"),HullChanged < 32);
				TestTrue(TEXT("nebula rendering leaves the interior fixed"),Room->GetActorTransform().Equals(RoomBefore));
			}
			GasVisible(true);
			Ship->SpacePose = OriginalPose; Graph->RefreshSpaceFrame(); Capture->SetWorldTransform(OriginalCamera); Capture->FOVAngle = 80;
		}
		CaptureStars(TEXT("yacht-window-stars.png"), StarsBefore);
		const FVector LandmarkViewLocation = Capture->GetComponentLocation();
		const FRotator LandmarkViewRotation = Capture->GetComponentRotation();
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		FVector PlanetCenter = FVector::ZeroVector;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UPrimitiveComponent*> Components; It->GetComponents(Components);
			for (UPrimitiveComponent* Component : Components)
			{
				if (Component->ComponentHasTag(TEXT("SWGSpacePlanetSurface")) && PlanetCenter.IsNearlyZero()) PlanetCenter = Component->GetComponentLocation();
				if (Component->ComponentHasTag(TEXT("SWGSpacePlanetSurface")) || Component->ComponentHasTag(TEXT("SWGSpacePlanetCloud"))
					|| Component->ComponentHasTag(TEXT("SWGSpacePlanetHalo")) || Component->ComponentHasTag(TEXT("SWGSpacePlanetAtmosphere"))) Capture->ShowOnlyComponent(Component);
			}
		}
		Capture->SetWorldLocation(FVector::ZeroVector); Capture->SetWorldRotation(PlanetCenter.Rotation());
		TArray<FColor> PlanetPixels; CapturePixels(TEXT("space-planet-tatooine.png"), PlanetPixels);
		int32 LitPlanetPixels = 0;
		for (const FColor& Pixel : PlanetPixels) LitPlanetPixels += Pixel.R > 20 || Pixel.G > 20 || Pixel.B > 20;
		TestTrue(TEXT("planet landmarks actually render"), LitPlanetPixels > 1000);
		Capture->ShowOnlyComponents.Reset();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UPrimitiveComponent*> Components; It->GetComponents(Components);
			for (UPrimitiveComponent* Component : Components)
				if (Component->ComponentHasTag(TEXT("SWGSpacePlanetSurface"))) Capture->ShowOnlyComponent(Component);
		}
		TArray<FColor> SurfacePixels; CapturePixels(TEXT("space-planet-surface.png"), SurfacePixels);
		int32 LitSurfacePixels = 0;
		for (const FColor& Pixel : SurfacePixels) LitSurfacePixels += Pixel.R > 20 || Pixel.G > 20 || Pixel.B > 20;
		TestTrue(TEXT("textured planet renders independently of halo"), LitSurfacePixels > 1000);
		bool bDayNightChecked = false;
		int32 AtmosphereIndex = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UPrimitiveComponent*> Components; It->GetComponents(Components);
			for (UPrimitiveComponent* Atmosphere : Components)
			{
				if (!Atmosphere->ComponentHasTag(TEXT("SWGSpacePlanetAtmosphere"))) continue;
				Capture->ShowOnlyComponents.Reset();
				UPrimitiveComponent* Surface = nullptr;
				for (USceneComponent* Child : Atmosphere->GetAttachParent()->GetAttachChildren())
					if (Child->ComponentHasTag(TEXT("SWGSpacePlanetSurface"))) Surface = Cast<UPrimitiveComponent>(Child);
				if (!TestNotNull(TEXT("atmosphere belongs to a planet surface"), Surface)) continue;
				const FVector PlanetLocation = Surface->GetComponentLocation();
				const FVector Direction = PlanetLocation.GetSafeNormal();
				Capture->SetWorldLocation(PlanetLocation-Direction*Surface->Bounds.BoxExtent.GetMax()*3);
				Capture->SetWorldRotation(Direction.Rotation()); Capture->ShowOnlyComponent(Surface);
				TArray<FColor> Bare, Haze;
				const bool bPrimary = AtmosphereIndex == 0;
				const FString SurfaceName = bPrimary ? FString(TEXT("tatooine-isolated-surface.png")) : FString::Printf(TEXT("planet-%d-isolated-surface.png"), AtmosphereIndex);
				const FString HazeName = bPrimary ? FString(TEXT("tatooine-isolated-atmosphere.png")) : FString::Printf(TEXT("planet-%d-isolated-atmosphere.png"), AtmosphereIndex);
				++AtmosphereIndex;
				CapturePixels(*SurfaceName, Bare);
				Capture->ShowOnlyComponent(Atmosphere);
				CapturePixels(*HazeName, Haze);
				int32 Changed = 0;
				for (int32 Index = 0; Index < FMath::Min(Bare.Num(), Haze.Num()); ++Index)
					Changed += FMath::Abs(int32(Bare[Index].R)-int32(Haze[Index].R)) > 3;
				TestTrue(TEXT("thin planetary haze changes rendered pixels"), Changed > 50);
				const USWGSpacePlanetComponent* PlanetField = Game->GetSubsystem<USWGTerrainSubsystem>()->SpacePlanet;
				if (!bDayNightChecked && PlanetField && PlanetField->HasSun())
				{
					// The primary planet with all its layers, seen from the sun side, the far side and the terminator.
					bDayNightChecked = true;
					Capture->ShowOnlyComponents.Reset();
					for (USceneComponent* Layer : Atmosphere->GetAttachParent()->GetAttachChildren())
						if (UPrimitiveComponent* LayerPrimitive = Cast<UPrimitiveComponent>(Layer)) Capture->ShowOnlyComponent(LayerPrimitive);
					const FVector SunDirection = PlanetField->GetSunDirection();
					const FVector Sideways = FVector::CrossProduct(SunDirection, FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
					auto MeanBrightness = [&](const FVector& SideOfPlanet, const TCHAR* Name)
					{
						Capture->SetWorldLocation(PlanetLocation+SideOfPlanet*Surface->Bounds.BoxExtent.GetMax()*3);
						Capture->SetWorldRotation((-SideOfPlanet).Rotation());
						TArray<FColor> Pixels; CapturePixels(Name, Pixels);
						double Total = 0;
						for (const FColor& Pixel : Pixels) Total += FMath::Max3(Pixel.R, Pixel.G, Pixel.B);
						return Pixels.IsEmpty() ? 0.0 : Total/Pixels.Num();
					};
					const double DaySide = MeanBrightness(SunDirection, TEXT("tatooine-day-side.png"));
					const double NightSide = MeanBrightness(-SunDirection, TEXT("tatooine-night-side.png"));
					const double Terminator = MeanBrightness(Sideways, TEXT("tatooine-terminator.png"));
					AddInfo(FString::Printf(TEXT("planet mean brightness: day %.2f terminator %.2f night %.2f, sun radiance %s"), DaySide, Terminator, NightSide, *PlanetField->GetSunRadiance().ToString()));
					TestTrue(TEXT("day side of the planet is brighter than its night side"), DaySide > NightSide*1.5 && DaySide > NightSide+3);
					TestTrue(TEXT("terminator view lies between day and night"), Terminator > NightSide && Terminator < DaySide);
				}
			}
		}
		// Stages 5 and 9: the zone's globe terrain on the primary planet, seen from the launch point, from outside and from the far side.
		{
			USWGSpacePlanetComponent* RegionField = Game->GetSubsystem<USWGTerrainSubsystem>()->SpacePlanet;
			const double RegionWaitStart = FPlatformTime::Seconds();
			while (RegionField && RegionField->GetRegion().bPending && FPlatformTime::Seconds()-RegionWaitStart < 300)
			{
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread); FPlatformProcess::Sleep(.05f);
			}
			if (TestNotNull(TEXT("planet component for the region captures"), RegionField) && TestTrue(TEXT("Tatooine's region is ready for rendering"), RegionField->GetRegion().bReady))
			{
				const USWGSpacePlanetComponent::FPlanetVisual& RegionPlanet = RegionField->Planets[RegionField->GetRegion().PlanetIndex];
				const FVector PlanetLocation = RegionPlanet.Surface->GetComponentLocation();
				const FVector Direction = PlanetLocation.GetSafeNormal();
				const float PlanetRadius = RegionPlanet.Surface->Bounds.BoxExtent.GetMax();
				const float OriginalFov = Capture->FOVAngle;
				const FString RegionDirectory = FPaths::ProjectSavedDir()/TEXT("Automation/SpaceWindows");
				const FString EvidenceDirectory = FPaths::ProjectDir()/TEXT(".agent_tmp/terrain-quality-captures");
				IFileManager::Get().MakeDirectory(*EvidenceDirectory, true);
				auto CaptureRegion = [&](const TCHAR* Name, TArray<FColor>& Pixels)
				{
					CapturePixels(Name, Pixels);
					IFileManager::Get().Copy(*(EvidenceDirectory/Name), *(RegionDirectory/Name));
				};
				// The key light is turned toward the launch side so the region is day-lit; it is restored below.
				ADirectionalLight* KeyLight = Game->GetSubsystem<USWGTerrainSubsystem>()->SpaceLights[0];
				const FRotator OriginalLightRotation = KeyLight->GetActorRotation();
				const FVector TowardLight = (-Direction + FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal()*.35).GetSafeNormal();
				KeyLight->SetActorRotation((-TowardLight).Rotation());
				auto ShowPlanetLayers = [&](bool bSurfaceOnly)
				{
					Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
					Capture->ShowOnlyComponents.Reset();
					for (USceneComponent* Layer : RegionPlanet.Surface->GetAttachParent()->GetAttachChildren())
						if (UPrimitiveComponent* LayerPrimitive = Cast<UPrimitiveComponent>(Layer))
							if (!bSurfaceOnly || LayerPrimitive == RegionPlanet.Surface) Capture->ShowOnlyComponent(LayerPrimitive);
				};
				auto CountDifferent = [](const TArray<FColor>& First, const TArray<FColor>& Second)
				{
					int32 Different = 0;
					for (int32 Index = 0; Index < FMath::Min(First.Num(), Second.Num()); ++Index)
						Different += FMath::Abs(int32(First[Index].R)-int32(Second[Index].R)) > 12 || FMath::Abs(int32(First[Index].G)-int32(Second[Index].G)) > 12
							|| FMath::Abs(int32(First[Index].B)-int32(Second[Index].B)) > 12;
					return Different;
				};
				auto CountLit = [](const TArray<FColor>& Pixels)
				{
					int32 Lit = 0;
					for (const FColor& Pixel : Pixels) Lit += Pixel.R > 20 || Pixel.G > 20 || Pixel.B > 20;
					return Lit;
				};
				// From the launch point: the default view with every layer, then the surface alone with and without the terrain.
				ShowPlanetLayers(false);
				Capture->FOVAngle = 80; Capture->SetWorldLocation(FVector::ZeroVector); Capture->SetWorldRotation(Direction.Rotation());
				TArray<FColor> DefaultView; CaptureRegion(TEXT("tatooine-globe-default-view.png"), DefaultView);
				ShowPlanetLayers(true);
				// Wide enough to see well toward the limb of the visible cap, so most of the frame is globe terrain.
				Capture->FOVAngle = 120;
				TArray<FColor> LaunchWithGlobe, LaunchWithout; CaptureRegion(TEXT("tatooine-globe-launch-wide.png"), LaunchWithGlobe);
				RegionPlanet.SurfaceMaterial->SetScalarParameterValue(TEXT("RegionEnabled"), 0.f);
				CaptureRegion(TEXT("tatooine-generic-launch-wide.png"), LaunchWithout);
				RegionPlanet.SurfaceMaterial->SetScalarParameterValue(TEXT("RegionEnabled"), 1.f);
				const int32 LaunchLit = CountLit(LaunchWithGlobe);
				const int32 LaunchChanged = CountDifferent(LaunchWithGlobe, LaunchWithout);
				AddInfo(FString::Printf(TEXT("launch view, surface alone: %d lit pixels, %d differ from the generic surface"), LaunchLit, LaunchChanged));
				TestTrue(TEXT("the launch view is mostly planet surface"), LaunchLit > 120000);
				TestTrue(TEXT("most of the launch view is globe terrain, not the generic surface"), LaunchChanged > LaunchLit/2);
				// A look toward the edge of the visible cap, 18 degrees off the centre line.
				const FVector Sideways = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
				Capture->FOVAngle = 70; Capture->SetWorldRotation((Direction + Sideways*FMath::Tan(FMath::DegreesToRadians(18.f))).GetSafeNormal().Rotation());
				TArray<FColor> EdgeView; CaptureRegion(TEXT("tatooine-globe-launch-edge.png"), EdgeView);
				TestTrue(TEXT("the edge of the visible cap is lit terrain"), CountLit(EdgeView) > 20000);

				// From outside: the whole front of the planet, the real terrain in the middle and the generated ground toward the limb.
				Capture->FOVAngle = 34; Capture->SetWorldLocation(PlanetLocation - Direction*PlanetRadius*2.4); Capture->SetWorldRotation(Direction.Rotation());
				TArray<FColor> WithRegion, WithoutRegion; CaptureRegion(TEXT("tatooine-globe-outside.png"), WithRegion);
				RegionPlanet.SurfaceMaterial->SetScalarParameterValue(TEXT("RegionEnabled"), 0.f);
				CaptureRegion(TEXT("tatooine-generic-outside.png"), WithoutRegion);
				RegionPlanet.SurfaceMaterial->SetScalarParameterValue(TEXT("RegionEnabled"), 1.f);
				const int32 RegionLitPixels = CountLit(WithRegion), RegionPixelsChanged = CountDifferent(WithRegion, WithoutRegion);
				AddInfo(FString::Printf(TEXT("planet seen from outside: %d lit pixels, %d differ from the generic surface"), RegionLitPixels, RegionPixelsChanged));
				TestTrue(TEXT("the outside view renders a lit planet"), RegionLitPixels > 20000);
				TestTrue(TEXT("the outside view differs from the generic surface"), RegionPixelsChanged > 5000);

				// The far side, from the opposite direction.
				ShowPlanetLayers(true);
				Capture->FOVAngle = 34; Capture->SetWorldLocation(PlanetLocation + Direction*PlanetRadius*2.4); Capture->SetWorldRotation((-Direction).Rotation());
				KeyLight->SetActorRotation((-Direction).Rotation());
				TArray<FColor> FarSide; CaptureRegion(TEXT("tatooine-globe-far-side.png"), FarSide);
				TestTrue(TEXT("the far side renders a lit planet"), CountLit(FarSide) > 20000);
				ShowPlanetLayers(false);
				Capture->FOVAngle = 40; Capture->SetWorldLocation(PlanetLocation + Direction*PlanetRadius*3); Capture->SetWorldRotation((-Direction).Rotation());
				TArray<FColor> FarSideAll; CaptureRegion(TEXT("tatooine-globe-far-side-all-layers.png"), FarSideAll);
				KeyLight->SetActorRotation(OriginalLightRotation);
				Capture->FOVAngle = OriginalFov; Capture->ShowOnlyComponents.Reset();
				Capture->SetWorldLocation(LandmarkViewLocation); Capture->SetWorldRotation(LandmarkViewRotation);
			}
		}
		const FTransform DustRoomPose = Room->GetActorTransform();
		Capture->SetWorldLocation(LandmarkViewLocation); Capture->SetWorldRotation(LandmarkViewRotation);
		USWGSpaceDustComponent* DustField = Game->GetSubsystem<USWGTerrainSubsystem>()->SpaceDust;
		UInstancedStaticMeshComponent* Dust = DustField ? DustField->Dust.Get() : nullptr;
		if (TestNotNull(TEXT("space dust available for rendered validation"), Dust))
		{
			FVector HullCenter, HullExtent; Ship->GetActorBounds(false, HullCenter, HullExtent);
			const FBox HullBounds(HullCenter-HullExtent, HullCenter+HullExtent);
			TestFalse(TEXT("loaded hull has clipping bounds"), HullExtent.IsNearlyZero());
			const TArray<TObjectPtr<ADirectionalLight>>& DustLights = Game->GetSubsystem<USWGTerrainSubsystem>()->SpaceLights;
			const FString DustDirectory = FPaths::ProjectSavedDir()/TEXT("Automation/SpaceWindows");
			// Dust is advanced without a terrain tick so the movement sample survives into the capture.
			auto MoveShipTo = [&](const FVector& SpacePosition, float DeltaTime)
			{
				Ship->SpacePose.SetLocation(SpacePosition); Graph->RefreshSpaceFrame();
				DustField->Update(Capture->GetComponentLocation(), DustLights, DeltaTime);
			};
			auto CaptureDust = [&](const TCHAR* Name, TArray<FColor>& Pixels)
			{
				Capture->ShowOnlyComponents.Reset(); Capture->ShowOnlyComponent(Dust);
				Capture->SetWorldLocation(LandmarkViewLocation); Capture->SetWorldRotation(LandmarkViewRotation);
				World->SendAllEndOfFrameUpdates(); Capture->CaptureScene();
				TestTrue(TEXT("dust capture readable"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
				TArray64<uint8> DustPng; FImageUtils::PNGCompressImageArray(512,512,Pixels,DustPng);
				TestTrue(TEXT("dust evidence saved"), FFileHelper::SaveArrayToFile(DustPng, *(DustDirectory/Name)));
				int32 Lit = 0;
				for (const FColor& Pixel : Pixels) Lit += Pixel.R > 20 || Pixel.G > 20 || Pixel.B > 20;
				return Lit;
			};
			auto CheckOutsideHull = [&]
			{
				const FVector Streak = DustField->GetStreakVector();
				for (int32 Instance = 0; Instance < Dust->GetInstanceCount(); ++Instance)
				{
					FTransform Transform; Dust->GetInstanceTransform(Instance, Transform, true);
					if (Transform.GetScale3D().IsNearlyZero()) continue;
					TestFalse(TEXT("dust starts outside POB hull"), HullBounds.IsInside(Transform.GetLocation()));
					TestFalse(TEXT("dust trail ends outside POB hull"), HullBounds.IsInside(Transform.GetLocation()+Streak));
				}
			};
			TArray<FColor> StationaryPixels, SlowPixels, CruisePixels, BoostPixels;
			MoveShipTo(FVector::ZeroVector, 1.f/60.f); MoveShipTo(FVector::ZeroVector, 1.f/60.f);
			TestTrue(TEXT("stationary dust has no streak"), DustField->GetStreakVector().IsNearlyZero());
			const int32 StationaryLit = CaptureDust(TEXT("space-dust-stationary.png"), StationaryPixels);
			// Per-frame displacement: below the cap, then far above it. The cap is one native metre.
			MoveShipTo(FVector(20,0,0), 1.f/60.f);
			const double SlowStreak = DustField->GetStreakVector().Size();
			CaptureDust(TEXT("space-dust-slow.png"), SlowPixels);
			MoveShipTo(FVector(2020,0,0), 1.f/60.f);
			const double CruiseStreak = DustField->GetStreakVector().Size();
			TestTrue(TEXT("fast motion dust streak capped at one metre"), CruiseStreak <= 100.01);
			TestTrue(TEXT("a faster ship leaves a longer streak vector"), CruiseStreak > SlowStreak+1.0);
			CheckOutsideHull();
			const int32 CruiseLit = CaptureDust(TEXT("space-dust-cruise.png"), CruisePixels);
			DustField->SetStreakBoost(4.f);
			MoveShipTo(FVector(4020,0,0), 1.f/60.f);
			CheckOutsideHull();
			const int32 BoostLit = CaptureDust(TEXT("space-dust-boost.png"), BoostPixels);
			DustField->SetStreakBoost(1.f);
			// The booster drives the boost: 1 up to MaxSpeed, a smoothstep to 3 at boosterMaxSpeed (baseline-1 scalar 18).
			TestEqual(TEXT("boost is 1 at MaxSpeed"), USWGSpaceDustComponent::ComputeStreakBoost(100.f, 100.f, 200.f), 1.f);
			TestEqual(TEXT("boost is 1 below MaxSpeed"), USWGSpaceDustComponent::ComputeStreakBoost(10.f, 100.f, 200.f), 1.f);
			TestEqual(TEXT("boost is 2 halfway to boost speed"), USWGSpaceDustComponent::ComputeStreakBoost(150.f, 100.f, 200.f), 2.f);
			TestEqual(TEXT("boost is 3 at boost speed"), USWGSpaceDustComponent::ComputeStreakBoost(200.f, 100.f, 200.f), 3.f);
			TestEqual(TEXT("boost stays 3 beyond boost speed"), USWGSpaceDustComponent::ComputeStreakBoost(900.f, 100.f, 200.f), 3.f);
			TestEqual(TEXT("without MaxSpeed the boost stays 1"), USWGSpaceDustComponent::ComputeStreakBoost(500.f, 0.f, 200.f), 1.f);
			TestEqual(TEXT("without a booster speed the ramp spans half of MaxSpeed"), USWGSpaceDustComponent::ComputeStreakBoost(125.f, 100.f, 0.f), 2.f);
			float PreviousBoost = 1.f; bool bBoostMonotonic = true;
			for (float SpeedMeters = 0.f; SpeedMeters <= 250.f; SpeedMeters += 5.f)
			{
				const float Boost = USWGSpaceDustComponent::ComputeStreakBoost(SpeedMeters, 100.f, 200.f);
				bBoostMonotonic &= Boost >= PreviousBoost-UE_KINDA_SMALL_NUMBER && Boost <= USWGSpaceDustComponent::MaxStreakBoost; PreviousBoost = Boost;
			}
			TestTrue(TEXT("boost rises monotonically and never passes the maximum"), bBoostMonotonic);
			Ship->ShipState.Flight.MaxSpeed = 100.f; Ship->ShipState.bHasBase1 = true; Ship->ShipState.OwnerValues[18] = 200.f;
			for (const TPair<float, float>& Expected : {TPair<float, float>(50.f, 1.f), TPair<float, float>(150.f, 2.f), TPair<float, float>(300.f, 3.f)})
			{
				Ship->SpaceVelocity = FVector(Expected.Key*SWGWorldScale, 0, 0);
				MoveShipTo(FVector(4020,0,0), 1.f/60.f);
				TestTrue(*FString::Printf(TEXT("the ship at %.0f m/s drives the streak boost to %.0f"), Expected.Key, Expected.Value), FMath::IsNearlyEqual(DustField->GetStreakBoost(), Expected.Value, .01f));
			}
			Ship->SpaceVelocity = FVector::ZeroVector; Ship->ShipState.Flight.MaxSpeed = 0.f; Ship->ShipState.bHasBase1 = false; Ship->ShipState.OwnerValues[18] = 0.f;
			DustField->SetStreakBoost(1.f);
			AddInfo(FString::Printf(TEXT("dust lit pixels: stationary=%d cruise=%d boost=%d; streak slow=%.1f cruise=%.1f"), StationaryLit, CruiseLit, BoostLit, SlowStreak, CruiseStreak));
			TestTrue(TEXT("moving dust actually renders"), CruiseLit > 20);
			TestTrue(TEXT("cruise streaks cover more pixels than stationary dots"), CruiseLit > StationaryLit);
			TestTrue(TEXT("boost streaks are longer than cruise streaks"), BoostLit > CruiseLit);

			// Debris: only inside the zone's asteroid fields, and recycled by reflecting about the camera.
			if (TestFalse(TEXT("zone authors asteroid fields"), DustField->Fields.IsEmpty()) && IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Space.Debris"))->GetBool())
			{
				int32 DebrisBudget = 0;
				for (const USWGSpaceDustComponent::FDebrisStyle& FieldStyle : DustField->FieldStyles) DebrisBudget = FMath::Max(DebrisBudget, FieldStyle.Count);
				TestEqual(TEXT("debris piece count is the largest field budget"), DustField->Debris.Num(), DebrisBudget);
				bool bMeshesLoaded = false;
				for (int32 Attempt = 0; Attempt < 1500 && !bMeshesLoaded; ++Attempt)
				{
					Meshes->Tick(1.f/60.f);
					FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
					bMeshesLoaded = DustField->DebrisMeshes.Num() > 0;
					for (const TObjectPtr<UInstancedStaticMeshComponent>& Layer : DustField->DebrisMeshes) bMeshesLoaded &= Layer != nullptr;
					if (!bMeshesLoaded) FPlatformProcess::Sleep(.01f);
				}
				if (TestTrue(TEXT("retail debris meshes load"), bMeshesLoaded))
				{
					int32 DebrisInstances = 0;
					for (const TObjectPtr<UInstancedStaticMeshComponent>& Layer : DustField->DebrisMeshes) DebrisInstances += Layer->GetInstanceCount();
					TestEqual(TEXT("every debris piece has an instance"), DebrisInstances, DustField->Debris.Num());
					const FSWGAsteroidFieldData& Field = DustField->Fields[0];
					const FVector NativeSpot = Field.bSpline && !Field.ControlPoints.IsEmpty() ? Field.ControlPoints[0] : Field.Center;
					const FVector FieldSpot = FVector(NativeSpot.Z, NativeSpot.X, NativeSpot.Y)*100.0;
					MoveShipTo(FieldSpot, 1.f/60.f);
					TestTrue(TEXT("debris is active inside an asteroid field"), DustField->IsDebrisActive());
					int32 ShownRocks = 0;
					for (const TObjectPtr<UInstancedStaticMeshComponent>& Layer : DustField->DebrisMeshes)
					{
						TestTrue(TEXT("debris layer is visible in the field"), Layer->IsVisible());
						for (int32 Instance = 0; Instance < Layer->GetInstanceCount(); ++Instance)
						{
							FTransform Transform; Layer->GetInstanceTransform(Instance, Transform, true);
							ShownRocks += !Transform.GetScale3D().IsNearlyZero();
						}
					}
					TestTrue(TEXT("most debris rocks are placed around the ship"), ShownRocks > DustField->GetActiveDebrisStyle().Count/2);
					TestTrue(TEXT("no more rocks are shown than the active style's count"), ShownRocks <= DustField->GetActiveDebrisStyle().Count);
					// A jump of twice the radius pushes every rock out of range at once; each must reappear mirrored about the new camera.
					const FVector CameraBefore = Graph->GetSpaceFrame().TransformPosition(Capture->GetComponentLocation());
					TArray<FVector> RelativeBefore;
					for (const USWGSpaceDustComponent::FDebrisPiece& Piece : DustField->Debris) RelativeBefore.Add(Piece.Position-CameraBefore);
					const double DebrisRange = DustField->GetActiveDebrisStyle().DeleteRange;
					MoveShipTo(FieldSpot+FVector(2.0*DebrisRange,0,0), 0.f);
					const FVector CameraAfter = Graph->GetSpaceFrame().TransformPosition(Capture->GetComponentLocation());
					for (int32 Piece = 0; Piece < DustField->Debris.Num(); ++Piece)
					{
						const FVector Relative = DustField->Debris[Piece].Position-CameraAfter;
						TestTrue(TEXT("recycled debris stays within the debris radius"), Relative.Size() <= DebrisRange+1.0);
						TestTrue(TEXT("recycled debris is mirrored about the camera"), Relative.Equals(-RelativeBefore[Piece], 1.0));
					}
				}
			}
		}
		TestTrue(TEXT("dust motion leaves the room fixed"), Room->GetActorTransform().Equals(DustRoomPose));
		Ship->SpacePose.SetLocation(FVector::ZeroVector); Graph->RefreshSpaceFrame();
		Capture->ShowOnlyComponents.Reset(); Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
		Capture->SetWorldLocation(LandmarkViewLocation); Capture->SetWorldRotation(LandmarkViewRotation);
		int32 StarPixels = 0;
		for (const FColor& Pixel : StarsBefore) StarPixels += Pixel.R > 20 || Pixel.G > 20 || Pixel.B > 20;
		TestTrue(TEXT("space stars actually render"), StarPixels > 50);
		auto RedCentroid = [](const TArray<FColor>& Pixels)
		{
			FVector2D Sum = FVector2D::ZeroVector; int32 Count = 0;
			for (int32 Index = 0; Index < Pixels.Num(); ++Index)
				if (Pixels[Index].R > 80 && Pixels[Index].R > Pixels[Index].G*2 && Pixels[Index].R > Pixels[Index].B*2)
				{ Sum += FVector2D(Index%512, Index/512); ++Count; }
			return Count ? Sum/Count : FVector2D::ZeroVector;
		};
		for (TActorIterator<AActor> It(World); It; ++It)
			if (*It != Marker && *It != CaptureActor) Capture->HiddenActors.Add(*It);
		CapturePixels(TEXT("yacht-window-marker-control.png"), Control);
		Capture->HiddenActors.Reset();
		int32 RedControl = 0;
		for (const FColor& Pixel : Control) RedControl += Pixel.R > 80 && Pixel.R > Pixel.G*2 && Pixel.R > Pixel.B*2;
		TestTrue(TEXT("marker renders at calibrated fixed exposure before occlusion checks"), RedControl > 100);
		CapturePixels(TEXT("yacht-window-before.png"), Before);
		const FVector CameraBefore = Capture->GetComponentLocation();
		Capture->SetWorldLocation(CameraBefore + Capture->GetRightVector()*150);
		CapturePixels(TEXT("yacht-window-walk.png"), WalkView);
		TestTrue(TEXT("sideways interior viewpoint shifts external marker left"), RedCentroid(WalkView).X < RedCentroid(Before).X-2);
		Capture->SetWorldLocation(CameraBefore);
		Ship->SpacePose.SetRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(5.0)));
		Graph->RefreshSpaceFrame();
		CaptureStars(TEXT("yacht-window-stars-roll.png"), StarsRolled);
		int32 ChangedStarPixels = 0;
		for (int32 Pixel = 0; Pixel < FMath::Min(StarsBefore.Num(), StarsRolled.Num()); ++Pixel)
			ChangedStarPixels += FMath::Abs(int32(StarsBefore[Pixel].R)-int32(StarsRolled[Pixel].R)) > 20
				|| FMath::Abs(int32(StarsBefore[Pixel].G)-int32(StarsRolled[Pixel].G)) > 20
				|| FMath::Abs(int32(StarsBefore[Pixel].B)-int32(StarsRolled[Pixel].B)) > 20;
		TestTrue(TEXT("ship roll rotates rendered stars around the fixed interior"), ChangedStarPixels > 50);
		AddInfo(FString::Printf(TEXT("star pixels=%d changed by roll=%d"), StarPixels, ChangedStarPixels));
		CapturePixels(TEXT("yacht-window-roll.png"), RollView);
		TestTrue(TEXT("ship roll changes external window perspective"), FVector2D::Distance(RedCentroid(RollView), RedCentroid(Before)) > 2);
		TestTrue(TEXT("room frame remains fixed during ship roll"), Room->GetActorTransform().Equals(RoomBefore));
		Ship->SpacePose.SetRotation(FQuat::Identity);
		Ship->SpacePose.SetLocation(FVector(100000, 100000, 0)); Graph->RefreshSpaceFrame();
		CapturePixels(TEXT("yacht-window-after.png"), After);
		TestTrue(TEXT("rendered room frame stays fixed while ship travels"), Room->GetActorTransform().Equals(RoomBefore));
		int32 RedBefore = 0, RedAfter = 0;
		for (const FColor& Pixel : Before) RedBefore += Pixel.R > 80 && Pixel.R > Pixel.G*2 && Pixel.R > Pixel.B*2;
		for (const FColor& Pixel : After) RedAfter += Pixel.R > 80 && Pixel.R > Pixel.G*2 && Pixel.R > Pixel.B*2;
		TestTrue(TEXT("external marker is visible through real yacht window"), RedBefore > RedAfter+100);
		AddInfo(FString::Printf(TEXT("window red pixels before=%d after=%d; camera=%s; outward=%s"), RedBefore, RedAfter, *Capture->GetComponentLocation().ToString(), *Outward.ToString()));
		ASWGPlayer* Pilot = World->SpawnActor<ASWGPlayer>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		if (!TestNotNull(TEXT("exterior camera pilot fixture"), Pilot) || !TestNotNull(TEXT("exterior camera controller fixture"), Controller)) { Cleanup(); return true; }
		Controller->SetAsLocalPlayerController();
		Controller->Possess(Pilot);
		for (ASWGShip* CameraShip : {Ship, SelectedShip})
		{
			const bool bYacht = CameraShip == Ship;
			const TCHAR* Label = bYacht ? TEXT("yacht") : TEXT("YT1300");
			Graph->RegisterStaticObject(103, Pilot, bYacht ? 100 : 201);
			CameraShip->SetActorHiddenInGame(false);
			Graph->ContainmentTypeByObjectId.Add(103, int32(ESWGContainmentType::ShipPilotPOB));
			CameraShip->bHasFlightBaseline = true; CameraShip->TangibleComponent->bHasBase3 = true;
			CameraShip->SpacePose = FTransform::Identity; Graph->RefreshSpaceFrame();
			if (!TestTrue(TEXT("exterior fixture has a locally controlled pilot"), Pilot->IsLocallyControlled())
				|| !TestTrue(TEXT("exterior fixture reaches actual scene readiness"), Graph->IsLocalSpaceSceneReady())) { Cleanup(); return true; }
			Pilot->SetSceneLoadingLocked(false); Pilot->Tick(0);
			FMinimalViewInfo ExteriorView; Pilot->CalcCamera(.1f, ExteriorView);
			TestTrue(*FString::Printf(TEXT("actual %s defaults to exterior camera"), Label), Pilot->IsExteriorShipView());
			Capture->SetWorldLocation(ExteriorView.Location); Capture->SetWorldRotation(ExteriorView.Rotation); Capture->FOVAngle = ExteriorView.FOV;
			Capture->HiddenActors = Controller->HiddenActors;
			Capture->ShowFlags.SetLighting(false);
			Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			Capture->ShowOnlyComponents.Reset();
			for (UStaticMeshComponent* HullMesh : (bYacht ? HullComponents : SelectedHullComponents)) Capture->ShowOnlyComponent(HullMesh);
			TArray<FColor> ExteriorPixels;
			CapturePixels(bYacht ? TEXT("yacht-exterior-authored-camera.png") : TEXT("yt1300-exterior-authored-camera.png"), ExteriorPixels);
			int32 HullPixels = 0, EdgePixels = 0;
			for (int32 Index = 0; Index < ExteriorPixels.Num(); ++Index)
			{
				const FColor& Pixel = ExteriorPixels[Index];
				const bool bHullPixel = Pixel.R > 20 || Pixel.G > 20 || Pixel.B > 20;
				HullPixels += bHullPixel;
				EdgePixels += bHullPixel && (Index % 512 == 0 || Index % 512 == 511 || Index / 512 == 0 || Index / 512 == 511);
			}
			TestTrue(*FString::Printf(TEXT("actual %s hull renders from authored exterior camera"), Label), HullPixels > 100);
			TestEqual(*FString::Printf(TEXT("default exterior view frames the full %s without clipping"), Label), EdgePixels, 0);
			AddInfo(FString::Printf(TEXT("%s authored exterior camera=%s rotation=%s hull pixels=%d"), Label, *ExteriorView.Location.ToString(), *ExteriorView.Rotation.ToString(), HullPixels));

				// The same two views, kept for looking at: the default zoom and the furthest the wheel reaches.
				const FString PilotDirectory = FPaths::ProjectDir()/TEXT(".agent_tmp/pilot-camera-captures");
				IFileManager::Get().MakeDirectory(*PilotDirectory, true);
				auto SavePilotCapture = [&](const FString& Name, const TArray<FColor>& Pixels)
				{
					TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(512, 512, Pixels, Png);
					TestTrue(TEXT("pilot camera capture saved"), FFileHelper::SaveArrayToFile(Png, *(PilotDirectory/Name)));
				};
				SavePilotCapture(FString::Printf(TEXT("%s-default-zoom.png"), Label), ExteriorPixels);
				const double DefaultDistance = FVector::Dist(ExteriorView.Location, CameraShip->GetActorLocation());
				const TArray<float> NoAuthoredSteps;
				const bool bHasCockpit = CameraShip->CockpitData.IsSet();
				const ASWGPlayer::FShipZoomSteps Steps = ASWGPlayer::BuildShipZoomSteps(bHasCockpit ? CameraShip->CockpitData->ZoomDistances : NoAuthoredSteps,
					bHasCockpit ? CameraShip->CockpitData->FirstPersonThreshold : 0.f, ASWGPlayer::GetShipZoomMultiplier());
				if (Steps.bAuthored)
					TestTrue(*FString::Printf(TEXT("default %s zoom is the widest authored step times the multiplier"), Label),
						FMath::IsNearlyEqual(DefaultDistance, Steps.Distances[Steps.DefaultIndex] * SWGWorldScale, .01));
				for (int32 StepCount = 0; StepCount < 20; ++StepCount) Pilot->StepShipZoom(1);
				FMinimalViewInfo FarthestView; Pilot->CalcCamera(1.f, FarthestView);
				const double FarthestDistance = FVector::Dist(FarthestView.Location, CameraShip->GetActorLocation());
				TestTrue(*FString::Printf(TEXT("%s wheel zoom stops at three times the default"), Label), Steps.bAuthored
					? FMath::IsNearlyEqual(FarthestDistance / DefaultDistance, 3., .01) : FarthestDistance > DefaultDistance * 2);
				Capture->SetWorldLocation(FarthestView.Location); Capture->SetWorldRotation(FarthestView.Rotation);
				TArray<FColor> FarthestPixels;
				CapturePixels(bYacht ? TEXT("yacht-exterior-max-zoom.png") : TEXT("yt1300-exterior-max-zoom.png"), FarthestPixels);
				SavePilotCapture(FString::Printf(TEXT("%s-max-zoom.png"), Label), FarthestPixels);
				int32 FarthestHullPixels = 0;
				for (const FColor& Pixel : FarthestPixels) FarthestHullPixels += Pixel.R > 20 || Pixel.G > 20 || Pixel.B > 20;
				TestTrue(*FString::Printf(TEXT("%s is still visible at maximum zoom"), Label), FarthestHullPixels > 20);
				AddInfo(FString::Printf(TEXT("%s zoom: default %.1f m, maximum %.1f m, hull pixels %d / %d"), Label, DefaultDistance / SWGWorldScale, FarthestDistance / SWGWorldScale, HullPixels, FarthestHullPixels));
		}
		Cleanup(); return true;
	}));
	return true;
}
#endif
