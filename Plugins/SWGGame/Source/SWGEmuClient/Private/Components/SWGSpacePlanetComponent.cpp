#include "Components/SWGSpacePlanetComponent.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGSpaceTerrainReader.h"
#include "TRE/SWGPlanetAppearanceReader.h"
#include "TRE/SWGShaderReader.h"
#include "Common/SWGWorldScale.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Generators/SphereGenerator.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"
#include "Common/SWGTerrainBake.h"
#include "Common/SWGPlanetGlobeBake.h"
#include "Common/SWGSpaceQuality.h"
#include "Async/Async.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Actor.h"

namespace
{
	TAutoConsoleVariable<float> CVarSpacePlanetRegionFill(
		TEXT("swg.Space.PlanetRegionFill"), 1.0f,
		TEXT("Cap fill fraction 0.5-2: the playable map's half width over the radius of the cap of the planet the viewer sees. 1 or more covers the whole "
			"visible cap with real terrain (more zooms in); less leaves a generated border. Replaces swg.Space.PlanetRegionSize. Next zone load."));
	TAutoConsoleVariable<float> CVarSpacePlanetCloudThinning(
		TEXT("swg.Space.PlanetCloudThinning"), 0.6f,
		TEXT("0-1: how much of the cloud layer's opacity is removed where the viewer looks straight down at the zone planet's terrain; 0 keeps the retail clouds. Live."));
	TAutoConsoleVariable<int32> CVarSpacePlanetRegionResolution(
		TEXT("swg.Space.PlanetRegionResolution"), 4096,
		TEXT("Pixels along each edge of the planet's cap map (128-4096; the whole-globe map is twice as wide, up to 4096); swg.Space.Quality sets it. Above 2048 the ground bake "
			"matches it (4096 = 4 m a pixel); at or below, the ground bake is capped at 1024. Cached per resolution. Next zone load."));
	TAutoConsoleVariable<float> CVarSpacePlanetDetailTile(
		TEXT("swg.Space.PlanetDetailTile"), 96.f,
		TEXT("Ground size, in raw terrain units (metres on the playable map), of one tile of the retail ground detail textures on the planet globe. Live."));
	TAutoConsoleVariable<float> CVarSpacePlanetDetailStrength(
		TEXT("swg.Space.PlanetDetailStrength"), .85f,
		TEXT("0-1: how much of the retail ground detail textures' variation the planet globe takes on, fading out where a tile gets small on screen; 0 is the bake alone. Live."));
	TAutoConsoleVariable<float> CVarSpaceSunIntensity(
		TEXT("swg.Space.SunIntensity"), 1.f,
		TEXT("Brightness multiplier of the code-built sun and stars (the retail four-point cross sprites they replace). Live."));
	TAutoConsoleVariable<float> CVarSpacePlanetRegionNormalStrength(
		TEXT("swg.Space.PlanetRegionNormalStrength"), .25f,
		TEXT("Strength 0-1 of the baked relief shading on the planet region; 0 is flat. Next zone load."));
	TAutoConsoleVariable<int32> CVarSpacePlanetRegion(
		TEXT("swg.Space.PlanetRegion"), 1,
		TEXT("Project the zone planet's baked ground terrain onto its globe; 0 keeps the generic surface. Next zone load."));
}

// Client art table keyed by the lowercase STAT appearance name. Colours marked "data" come from the average colour of the
// planet's halo texture or retail surface texture (measured offline); the rest are the lore-based fallbacks.
USWGSpacePlanetComponent::FAtmosphereStyle USWGSpacePlanetComponent::FindAtmosphereStyle(const FString& Appearance)
{
	struct FEntry { const TCHAR* Name; FLinearColor Color; FLinearColor Terminator; float Density; bool bOcean; float HaloIntensity; float HaloScale; };
	const FLinearColor Warm(1.f, .5f, .25f);
	static const FEntry Entries[] = {
		{TEXT("planet_tatooine"), FLinearColor(.65f, .38f, .16f), FLinearColor(1.f, .45f, .2f), .9f, false, 0.f, 1.f},	// dust haze (the old shell tint); no halo
		{TEXT("planet_tatooine_moon"), FLinearColor(.7f, .55f, .45f), Warm, .12f, false, .5f, .8f},	// data: pln_planet_moon_01 (.85,.71,.64)
		{TEXT("planet_tatooine_moon_02"), FLinearColor(.7f, .55f, .45f), Warm, .12f, false, .5f, .8f},
		{TEXT("planet_tatooine_moon_03"), FLinearColor(.7f, .55f, .45f), Warm, .12f, false, .5f, .8f},
		{TEXT("planet_naboo"), FLinearColor(.3f, .55f, 1.f), FLinearColor(1.f, .55f, .35f), 1.1f, true, .45f, .8f},	// data: planet_halo_top (.66,.82,1.0), saturated
		{TEXT("planet_naboo_moon"), FLinearColor(.25f, .6f, .65f), Warm, .25f, false, .5f, .8f},	// data: pln_naboo_blue (.21,.53,.54)
		{TEXT("planet_corellia"), FLinearColor(.45f, .62f, .95f), FLinearColor(1.f, .6f, .35f), .8f, true, .45f, .8f},	// fallback temperate blue
		{TEXT("planet_corl_moon_01"), FLinearColor(.2f, .56f, .51f), Warm, .2f, false, .5f, .8f},	// data: planet_corl_moon_01 (.20,.56,.51)
		{TEXT("planet_dathomir"), FLinearColor(.9f, .22f, .18f), FLinearColor(1.f, .3f, .15f), 1.f, false, .5f, .8f},	// red; data: surface (.58,.23,.25)
		{TEXT("planet_endor"), FLinearColor(.45f, .75f, .9f), Warm, .8f, false, .45f, .8f},	// data: planet_halo_top over a green-teal surface (.30,.45,.41)
		{TEXT("planet_gas_endor"), FLinearColor(.5f, .62f, .8f), Warm, 1.4f, false, .5f, .8f},	// data: gas giant surface (.45,.54,.65)
		{TEXT("planet_yavin"), FLinearColor(.95f, .65f, .35f), Warm, 1.3f, false, .5f, .8f},	// data: planet_yavin (.88,.66,.41)
		{TEXT("planet_yavin4"), FLinearColor(.35f, .8f, .65f), Warm, .9f, false, .45f, .8f},	// data: green-teal surface (.20,.58,.55)
		{TEXT("planet_dantooine"), FLinearColor(.5f, .7f, 1.f), Warm, .7f, false, .45f, .8f},	// data: planet_halo_top
		{TEXT("planet_lok"), FLinearColor(.35f, .6f, 1.f), Warm, .45f, false, .5f, .8f},	// data: planet_halo_top02 (.17,.60,1.0)
		{TEXT("planet_kessel"), FLinearColor(.75f, .62f, .5f), Warm, .5f, false, .5f, .8f},	// data: surface (.72,.58,.49)
		{TEXT("planet_09"), FLinearColor(.9f, .4f, .25f), Warm, .6f, false, .5f, .8f},	// data: intro_planet (.64,.37,.23)
		{TEXT("planet_kashyyyk"), FLinearColor(.3f, .7f, .65f), Warm, .9f, false, .45f, .8f},	// fallback green-blue; not in a space zone
	};
	const FString Key = FPaths::GetBaseFilename(Appearance).ToLower();
	FAtmosphereStyle Style;
	for (const FEntry& Entry : Entries)
	{
		if (Key != Entry.Name) continue;
		Style.Color = Entry.Color; Style.TerminatorColor = Entry.Terminator; Style.Density = Entry.Density; Style.bOcean = Entry.bOcean;
		Style.HaloIntensity = Entry.HaloIntensity; Style.HaloScale = Entry.HaloScale;
		break;
	}
	return Style;
}

bool USWGSpacePlanetComponent::Setup(const FSWGSpaceTerrainData& Data, USWGTreSubsystem* TreSubsystem, USWGMeshGeneratorSubsystem* MeshGenerator,
	FCreateSkySprite CreateSkySprite, TConstArrayView<ADirectionalLight*> SceneLights, const FString& TerrainVirtualPath)
{
	CancelRegionBake();
	Region = FRegionState(); RegionColourTexture = nullptr; RegionNormalTexture = nullptr; RegionFarTexture = nullptr; RegionDetailTexture = nullptr;
	MeshGeneratorRef = MeshGenerator; MainSunIndex = INDEX_NONE; RetainedRetailMaterials.Reset();
	// "terrain/space_tatooine.trn" -> "tatooine": the planet whose ground terrain has that name is the zone's primary.
	FString PrimaryName = FPaths::GetBaseFilename(TerrainVirtualPath).ToLower();
	if (!PrimaryName.RemoveFromStart(TEXT("space_"))) PrimaryName.Reset();
	// The second variants of a zone (space_naboo_2) share their planet.
	int32 SuffixIndex = INDEX_NONE;
	if (PrimaryName.FindLastChar(TEXT('_'), SuffixIndex) && SuffixIndex > 0 && PrimaryName.RightChop(SuffixIndex + 1).IsNumeric()) PrimaryName.LeftInline(SuffixIndex);
	Planets.Reset(); Celestials.Reset();
	AActor* SkyActor = GetOwner();
	SourceLights.Reset();
	for (UDirectionalLightComponent* Light : PlanetLights) if (IsValid(Light)) Light->DestroyComponent();
	PlanetLights.Reset();
	for (ADirectionalLight* SceneLight : SceneLights)
	{
		if (!IsValid(SceneLight)) continue;
		UDirectionalLightComponent* Light = NewObject<UDirectionalLightComponent>(SkyActor);
		Light->SetMobility(EComponentMobility::Movable); Light->SetAtmosphereSunLight(false); Light->SetUseTemperature(false);
		Light->SetCastShadows(false); Light->SetLightingChannels(false, true, false);
		Light->SetupAttachment(SkyActor->GetRootComponent()); Light->RegisterComponent();
		SourceLights.Add(SceneLight); PlanetLights.Add(Light);
	}
	TArray<FSWGPlanetAppearanceData> Appearances;
	double NearestSurface = TNumericLimits<double>::Max(), FarthestSurface = 0;
	for (const FSWGSpacePlanet& Definition : Data.Planets)
	{
		FSWGPlanetAppearanceData& Planet = Appearances.AddDefaulted_GetRef();
		if (!FSWGPlanetAppearanceReader::Read(TreSubsystem->CreateIffReader(Definition.Appearance), Planet)) { UE_LOG(LogTemp, Warning, TEXT("USWGSpacePlanetComponent: cannot read %s"), *Definition.Appearance); return false; }
		const double OuterRadius = FMath::Max(Planet.Surface.Radius, Planet.Clouds.Shader.IsEmpty() ? 0.f : Planet.Clouds.Radius)/.98;
		NearestSurface = FMath::Min(NearestSurface, (Definition.NativePosition.Size()-OuterRadius)*SWGWorldScale);
		FarthestSurface = FMath::Max(FarthestSurface, (Definition.NativePosition.Size()+OuterRadius)*SWGWorldScale);
	}
	// STAT places planets tens of metres from the viewer (Tatooine's surface is 33 m away), which only works as background.
	BackgroundScale = NearestSurface > 1 ? FMath::Max(1.0, PlanetNearestDistance/NearestSurface) : 1.0;
	if (FarthestSurface > 0) BackgroundScale = FMath::Min(BackgroundScale, StarDomeRadius*.95/FarthestSurface);
	if (NearestSurface*BackgroundScale < PlanetNearestDistance)
		UE_LOG(LogTemp, Warning, TEXT("Space planets cannot all sit behind the zone and inside the star dome; nearest surface %.0f m"), NearestSurface*BackgroundScale/100);
	for (int32 PlanetIndex = 0; PlanetIndex < Data.Planets.Num(); ++PlanetIndex)
	{
		const FSWGSpacePlanet& Definition = Data.Planets[PlanetIndex];
		const FSWGPlanetAppearanceData& Planet = Appearances[PlanetIndex];
		USceneComponent* Root = NewObject<USceneComponent>(SkyActor);
		Root->SetupAttachment(SkyActor->GetRootComponent()); Root->RegisterComponent();
		const FVector Angles = Definition.NativeAnglesDegrees;
		const FQuat Rotation = FQuat(FVector::UpVector, FMath::DegreesToRadians(Angles.X))
			* FQuat(FVector::RightVector, FMath::DegreesToRadians(Angles.Y))
			* FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Angles.Z));
		Root->SetRelativeTransform(FTransform(Rotation,
			FVector(Definition.NativePosition.Z, Definition.NativePosition.X, Definition.NativePosition.Y)*SWGWorldScale*BackgroundScale, FVector(BackgroundScale)));
		auto Surface = [&](const FSWGPlanetSurface& Layer, bool bCloud, TObjectPtr<UMaterialInstanceDynamic>& OutMaterial) -> UDynamicMeshComponent*
		{
			FSWGShaderData Shader;
			if (!FSWGShaderReader::ReadShader(TreSubsystem->CreateIffReader(Layer.Shader), Shader)) return nullptr;
			auto Texture = [&](const FString& Tag) -> UTexture2D*
			{
				const FSWGShaderTexture* Def = Shader.Textures.FindByPredicate([&](const FSWGShaderTexture& Entry) { return Entry.Tag == Tag; });
				return Def ? MeshGenerator->GetOrLoadObjectTexture(Def->VirtualPath) : nullptr;
			};
			UMaterialInterface* Material = nullptr;
			if (bCloud)
			{
				UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGPlanetCloud.M_SWGPlanetCloud"));
				UTexture2D* Main = Texture(TEXT("MAIN"));
				if (!Parent || !Main) return nullptr;
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, SkyActor);
				MID->SetTextureParameterValue(TEXT("Diffuse"), Main); Material = OutMaterial = MID;
			}
			else if (Shader.Textures.ContainsByPredicate([](const FSWGShaderTexture& Entry) { return Entry.Tag == TEXT("DTLA"); }))
			{
				UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGPlanetSurface.M_SWGPlanetSurface"));
				UTexture2D* DetailA = Texture(TEXT("DTLA")); UTexture2D* DetailB = Texture(TEXT("DTLB")); UTexture2D* Mask = Texture(TEXT("MASK"));
				if (!Parent || !DetailA || !DetailB || !Mask) return nullptr;
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, SkyActor);
				MID->SetTextureParameterValue(TEXT("DetailA"), DetailA); MID->SetTextureParameterValue(TEXT("DetailB"), DetailB);
				MID->SetTextureParameterValue(TEXT("Mask"), Mask); Material = OutMaterial = MID;
			}
			else if (Shader.Textures.ContainsByPredicate([](const FSWGShaderTexture& Entry) { return Entry.Tag == TEXT("MAIN"); }))
			{
				// Single-texture shaders (e_planet_detail/simple) go through the same material: MAIN fills both detail slots and a white mask keeps DetailA.
				UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGPlanetSurface.M_SWGPlanetSurface"));
				UTexture2D* Main = Texture(TEXT("MAIN"));
				UTexture* Neutral = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
				if (Parent && Main && Neutral)
				{
					UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, SkyActor);
					MID->SetTextureParameterValue(TEXT("DetailA"), Main); MID->SetTextureParameterValue(TEXT("DetailB"), Main);
					MID->SetTextureParameterValue(TEXT("Mask"), Neutral); Material = OutMaterial = MID;
				}
			}
			if (!Material) Material = MeshGenerator->GetOrBuildObjectMaterial(Layer.Shader);
			if (!Material) return nullptr;
			// shortcut: UE per-pixel lighting replaces retail vertex lighting; tune against a paired retail capture.
			UE::Geometry::FSphereGenerator Sphere;
			Sphere.Radius = Layer.Radius*SWGWorldScale;
			Sphere.NumPhi = FMath::Max(128, Planet.LatitudeSegments + (Planet.LatitudeSegments % 2)) + 1;
			Sphere.NumTheta = FMath::Max(256, Planet.LongitudeSegments + (Planet.LongitudeSegments % 2));
			Sphere.Generate();
			UE::Geometry::FDynamicMesh3 Mesh(&Sphere);
			Mesh.Attributes()->SetNumUVLayers(2);
			auto* UV = Mesh.Attributes()->PrimaryUV(); auto* DetailUV = Mesh.Attributes()->GetUVLayer(1);
			DetailUV->Copy(*UV);
			for (int32 Element : UV->ElementIndicesItr()) UV->SetElement(Element, UV->GetElement(Element)*Layer.UVRepeat);
			for (int32 Element : DetailUV->ElementIndicesItr()) DetailUV->SetElement(Element, DetailUV->GetElement(Element)*Layer.DetailUVRepeat);
			UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(SkyActor);
			Component->SetupAttachment(Root); Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCastShadow(false); Component->bAffectDistanceFieldLighting = false;
			Component->SetLightingChannels(false, true, false);
			Component->ComponentTags.Add(bCloud ? TEXT("SWGSpacePlanetCloud") : TEXT("SWGSpacePlanetSurface"));
			Component->RegisterComponent(); Component->SetMesh(MoveTemp(Mesh)); Component->SetMaterial(0, Material);
			return Component;
		};
		FPlanetVisual Visual;
		Visual.Surface = Surface(Planet.Surface, false, Visual.SurfaceMaterial); Visual.SurfaceSpin = Planet.Surface.SpinDegreesPerSecond;
		if (!Visual.Surface) { UE_LOG(LogTemp, Warning, TEXT("USWGSpacePlanetComponent: surface shader %s of %s did not load"), *Planet.Surface.Shader, *Definition.Appearance); return false; }
		if (!Planet.Clouds.Shader.IsEmpty())
		{
			Visual.Clouds = Surface(Planet.Clouds, true, Visual.CloudMaterial); Visual.CloudSpin = Planet.Clouds.SpinDegreesPerSecond;
			if (!Visual.Clouds) { UE_LOG(LogTemp, Warning, TEXT("USWGSpacePlanetComponent: cloud shader %s of %s did not load"), *Planet.Clouds.Shader, *Definition.Appearance); return false; }
		}
		const FAtmosphereStyle Style = FindAtmosphereStyle(Definition.Appearance);
		if (Visual.SurfaceMaterial)
		{
			Visual.SurfaceMaterial->SetVectorParameterValue(TEXT("AtmosphereTint"), Style.Color);
			Visual.SurfaceMaterial->SetScalarParameterValue(TEXT("OceanGlint"), Style.bOcean ? 1.f : 0.f);
			UTexture* CloudTexture = nullptr;
			if (Visual.CloudMaterial && Visual.CloudMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Diffuse")), CloudTexture))
			{
				Visual.SurfaceMaterial->SetTextureParameterValue(TEXT("CloudTexture"), CloudTexture);
				Visual.SurfaceMaterial->SetScalarParameterValue(TEXT("CloudRepeatU"), Planet.Clouds.UVRepeat.X);
				Visual.SurfaceMaterial->SetScalarParameterValue(TEXT("CloudRepeatV"), Planet.Clouds.UVRepeat.Y);
				Visual.SurfaceMaterial->SetScalarParameterValue(TEXT("CloudShadowStrength"), .8f);
			}
		}
		if (Visual.CloudMaterial) Visual.CloudMaterial->SetVectorParameterValue(TEXT("AtmosphereTint"), Style.Color);

		// Every planet gets the sun-aware shell, airless-looking moons included (their art table density is just low).
		UMaterialInterface* AtmosphereParent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGPlanetAtmosphere.M_SWGPlanetAtmosphere"));
		if (!AtmosphereParent) return false;
		Visual.AtmosphereMaterial = UMaterialInstanceDynamic::Create(AtmosphereParent, SkyActor);
		Visual.AtmosphereMaterial->SetVectorParameterValue(TEXT("AtmosphereTint"), Style.Color);
		Visual.AtmosphereMaterial->SetVectorParameterValue(TEXT("TerminatorTint"), Style.TerminatorColor);
		Visual.AtmosphereMaterial->SetScalarParameterValue(TEXT("Density"), Style.Density);
		Visual.AtmosphereMaterial->SetScalarParameterValue(TEXT("NightRim"), Style.NightRim);
		Visual.AtmosphereMaterial->SetScalarParameterValue(TEXT("ShellRatio"), AtmosphereShellRatio);
		// shortcut: a thin analytic shell, not full atmospheric scattering; upgrade for orbital descent.
		UDynamicMeshComponent* Atmosphere = NewObject<UDynamicMeshComponent>(SkyActor);
		Atmosphere->SetupAttachment(Root); Atmosphere->SetRelativeScale3D(FVector(1.f/AtmosphereShellRatio));
		Atmosphere->SetCollisionEnabled(ECollisionEnabled::NoCollision); Atmosphere->SetCastShadow(false);
		Atmosphere->bAffectDistanceFieldLighting = false; Atmosphere->SetLightingChannels(false, true, false);
		Atmosphere->ComponentTags.Add(TEXT("SWGSpacePlanetAtmosphere"));
		Atmosphere->RegisterComponent();
		Atmosphere->SetMesh(UE::Geometry::FDynamicMesh3(*Visual.Surface->GetMesh()));
		Atmosphere->SetMaterial(0, Visual.AtmosphereMaterial);
		Visual.Atmosphere = Atmosphere;

		// The retail halo stays as a dimmed outer glow; Tatooine's art entry drops it (its broad halo was replaced by the shell).
		if (!Planet.HaloShader.IsEmpty() && Style.HaloIntensity > 0)
		{
			Visual.Halo = CreateSkySprite(Planet.HaloShader, true);
			if (!Visual.Halo) { UE_LOG(LogTemp, Warning, TEXT("USWGSpacePlanetComponent: halo shader %s of %s did not load"), *Planet.HaloShader, *Definition.Appearance); return false; }
			Visual.Halo->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
			Visual.Halo->ComponentTags.Add(TEXT("SWGSpacePlanetHalo"));
			if (UMaterialInstanceDynamic* HaloMaterial = Cast<UMaterialInstanceDynamic>(Visual.Halo->GetMaterial(0)))
				HaloMaterial->SetScalarParameterValue(TEXT("Intensity"), Style.HaloIntensity);
			Visual.Halo->SetRelativeScale3D(FVector(Planet.Surface.Radius*Planet.HaloRadiusRatio*2*(Definition.HaloScale > 0 ? Definition.HaloScale : 1)*Style.HaloScale));
			Visual.HaloRoll = Definition.HaloAngleDegrees;
		}
		if (Visual.SurfaceMaterial) Visual.SurfaceMaterial->SetScalarParameterValue(TEXT("RegionEnabled"), 0.f);
		Planets.Add(Visual);

		const FString GroundTerrain = FSWGTerrainBake::FindGroundTerrain(*TreSubsystem, Definition.Appearance);
		if (Region.PlanetIndex == INDEX_NONE && !PrimaryName.IsEmpty() && Visual.SurfaceMaterial && FPaths::GetBaseFilename(GroundTerrain) == PrimaryName
			&& CVarSpacePlanetRegion.GetValueOnGameThread() != 0)
		{
			TArray<FString> DetailPaths;
			FSWGShaderData SurfaceShader;
			if (FSWGShaderReader::ReadShader(TreSubsystem->CreateIffReader(Planet.Surface.Shader), SurfaceShader))
				for (const FSWGShaderTexture& Texture : SurfaceShader.Textures)
					if (Texture.Tag == TEXT("DTLA") || Texture.Tag == TEXT("DTLB") || Texture.Tag == TEXT("MAIN")) DetailPaths.AddUnique(Texture.VirtualPath);
			FSWGPlanetGlobeParams GlobeParams;
			GlobeParams.CapRadius = ComputeCapRadius(Planet.Surface.Radius, Definition.NativePosition.Size());
			GlobeParams.Fill = FMath::Clamp(CVarSpacePlanetRegionFill.GetValueOnGameThread(), .5f, 2.f);
			GlobeParams.CapResolution = FSWGSpaceQuality::GetRegionResolution();
			GlobeParams.Seed = GetTypeHash(GroundTerrain);
			BeginRegionBake(Planets.Num() - 1, GroundTerrain, DetailPaths, GlobeParams, TreSubsystem);
		}
	}
	for (const FSWGSpaceCelestial& Definition : Data.Celestials)
	{
		const FVector Direction = FVector(Definition.NativeDirection.Z, Definition.NativeDirection.X, Definition.NativeDirection.Y).GetSafeNormal();
		auto Sprite = [&](const FString& Shader, float Size)
		{
			if (Shader.IsEmpty() || Size <= 0) return true;
			UStaticMeshComponent* Component = CreateSkySprite(Shader, true);
			if (!Component) return false;
			Component->ComponentTags.Add(TEXT("SWGSpaceCelestial"));
			Component->SetRelativeLocation(Direction*CelestialDistance);
			Component->SetRelativeScale3D(FVector(Size*CelestialDistance/100));
			FCelestialVisual Visual;
			Visual.Sprite = Component; Visual.Size = Size;
			// Deliberate departure: the retail star textures are four-point crosses (the starburst), so those sprites are redrawn as a disc and a corona
			// at the retail direction and size. The asset comes from swg.CreateSpacePlanetAssets; without it the retail sprite stays.
			if (IsCrossFlareShader(Shader))
			{
				UMaterialInterface* StarParent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGSunSprite.M_SWGSunSprite"));
				if (StarParent)
				{
					Visual.RetailMaterial = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0));
					RetainedRetailMaterials.Add(Visual.RetailMaterial);
					Visual.StarMaterial = UMaterialInstanceDynamic::Create(StarParent, SkyActor);
					Component->SetMaterial(0, Visual.StarMaterial);
				}
				else UE_LOG(LogTemp, Warning, TEXT("USWGSpacePlanetComponent: M_SWGSunSprite is missing (run swg.CreateSpacePlanetAssets); %s keeps its retail cross texture"), *Shader);
			}
			Celestials.Add(Visual); return true;
		};
		if (!Sprite(Definition.Shader, Definition.Size) || !Sprite(Definition.GlowShader, Definition.GlowSize)) { UE_LOG(LogTemp, Warning, TEXT("USWGSpacePlanetComponent: celestial sprite %s / %s did not load"), *Definition.Shader, *Definition.GlowShader); return false; }
	}
	// The largest star is the zone's sun: bigger, brighter and lit with the key light's colour; the rest are fainter stars.
	for (int32 CelestialIndex = 0; CelestialIndex < Celestials.Num(); ++CelestialIndex)
		if (Celestials[CelestialIndex].StarMaterial && (MainSunIndex == INDEX_NONE || Celestials[CelestialIndex].Size > Celestials[MainSunIndex].Size)) MainSunIndex = CelestialIndex;
	for (int32 CelestialIndex = 0; CelestialIndex < Celestials.Num(); ++CelestialIndex)
	{
		FCelestialVisual& Celestial = Celestials[CelestialIndex];
		if (!Celestial.StarMaterial) continue;
		Celestial.bMainSun = CelestialIndex == MainSunIndex;
		Celestial.StarMaterial->SetScalarParameterValue(TEXT("DiscIntensity"), Celestial.bMainSun ? 8.f : 3.f);
	}
	return true;
}

bool USWGSpacePlanetComponent::IsCrossFlareShader(const FString& ShaderName)
{
	// Checked against the extracted textures: starglow_back.dds and starglow.dds are a constant grey with a thin four-point cross in alpha;
	// starglow_radial.dds is a smooth round glow and stays as authored.
	const FString Key = FPaths::GetBaseFilename(ShaderName).ToLower();
	return Key == TEXT("cels_star_back") || Key == TEXT("starglow");
}

void USWGSpacePlanetComponent::Update(float DeltaTime, const FVector& Viewpoint)
{
	auto FaceViewer = [&](UStaticMeshComponent* Sprite, float Roll)
	{
		if (!IsValid(Sprite)) return;
		const FVector Direction = (Viewpoint-Sprite->GetComponentLocation()).GetSafeNormal();
		Sprite->SetWorldRotation(FQuat(Direction, FMath::DegreesToRadians(Roll))*FRotationMatrix::MakeFromZ(Direction).ToQuat());
	};
	bHasSun = false;
	for (int32 LightIndex = 0; LightIndex < PlanetLights.Num(); ++LightIndex)
	{
		const ADirectionalLight* Source = SourceLights[LightIndex].Get();
		UDirectionalLightComponent* Light = PlanetLights[LightIndex];
		if (!Source || !IsValid(Light)) continue;
		const UDirectionalLightComponent* SourceComponent = Source->GetComponent();
		Light->SetWorldRotation(SourceComponent->GetComponentQuat());
		// The fill light is only there so the night side is not pure black: neutral-cool, never the zone's reddish fill.
		const FLinearColor SourceColor = SourceComponent->GetLightColor();
		const float FillLuminance = SourceColor.GetLuminance();
		const FLinearColor FillColor = FMath::Lerp(SourceColor, FLinearColor(FillLuminance*.75f, FillLuminance*.85f, FillLuminance), PlanetFillDesaturation);
		Light->SetIntensity(SourceComponent->Intensity*(LightIndex == 0 ? 1.f : PlanetFillScale)); Light->SetLightColor(LightIndex == 0 ? SourceColor : FillColor);
		if (bHasSun) continue;
		// The key light: the materials want the direction toward it, and radiance as Unreal's Lambert diffuse sees it (lux / PI).
		SunDirection = -Light->GetForwardVector(); bHasSun = true;
		SunRadiance = Light->GetColoredLightBrightness()/UE_PI;
		// The sun's colour is the retail key light's, normalised so the star keeps its own brightness; softened toward white.
		const float StrongestChannel = FMath::Max3(SourceColor.R, SourceColor.G, SourceColor.B);
		if (StrongestChannel > KINDA_SMALL_NUMBER) SunStarColour = FMath::Lerp(FLinearColor::White, SourceColor/StrongestChannel, .75f);
	}
	for (FPlanetVisual& Planet : Planets)
	{
		if (IsValid(Planet.Surface)) Planet.Surface->AddLocalRotation(FRotator(0,Planet.SurfaceSpin*DeltaTime,0));
		if (IsValid(Planet.Clouds)) Planet.Clouds->AddLocalRotation(FRotator(0,Planet.CloudSpin*DeltaTime,0));
		FaceViewer(Planet.Halo, Planet.HaloRoll);
		if (!bHasSun) continue;
		const FLinearColor SunDirectionColor(SunDirection.X, SunDirection.Y, SunDirection.Z);
		for (UMaterialInstanceDynamic* Material : {Planet.SurfaceMaterial.Get(), Planet.CloudMaterial.Get(), Planet.AtmosphereMaterial.Get()})
		{
			if (!Material) continue;
			Material->SetVectorParameterValue(TEXT("SunDirection"), SunDirectionColor);
			Material->SetVectorParameterValue(TEXT("SunRadiance"), SunRadiance);
		}
		// Cloud shadows read the cloud texture in the cloud's frame, which has spun differently from the surface's.
		if (Planet.SurfaceMaterial && IsValid(Planet.Surface) && IsValid(Planet.Clouds))
			Planet.SurfaceMaterial->SetScalarParameterValue(TEXT("CloudYawOffset"),
				FMath::DegreesToRadians(Planet.Surface->GetRelativeRotation().Yaw - Planet.Clouds->GetRelativeRotation().Yaw));
	}
	// The viewer looks straight down at the globe terrain: thin the clouds there so the ground reads (live, from the cvar).
	if (Region.bReady && Planets.IsValidIndex(Region.PlanetIndex))
	{
		const FPlanetVisual& Primary = Planets[Region.PlanetIndex];
		if (Primary.CloudMaterial && IsValid(Primary.Surface))
		{
			const FVector ToViewer = (Viewpoint - Primary.Surface->GetComponentLocation()).GetSafeNormal();
			Primary.CloudMaterial->SetVectorParameterValue(TEXT("ThinDirection"), FLinearColor(ToViewer.X, ToViewer.Y, ToViewer.Z));
			Primary.CloudMaterial->SetScalarParameterValue(TEXT("ThinRadius"), Region.CapRadius);
			Primary.CloudMaterial->SetScalarParameterValue(TEXT("ThinStrength"), FMath::Clamp(CVarSpacePlanetCloudThinning.GetValueOnGameThread(), 0.f, 1.f));
		}
	}
	// The globe's detail textures: tile size and strength are live cvars; no families means no detail.
	if (Region.bReady && Planets.IsValidIndex(Region.PlanetIndex) && Planets[Region.PlanetIndex].SurfaceMaterial)
	{
		UMaterialInstanceDynamic* SurfaceMaterial = Planets[Region.PlanetIndex].SurfaceMaterial;
		SurfaceMaterial->SetScalarParameterValue(TEXT("DetailRepeat"), Region.GroundPerRadian/(FMath::Max(CVarSpacePlanetDetailTile.GetValueOnGameThread(), 1.f)*FMath::Square(FMath::Max(Region.CapRadius/.4f, 1.f))));
		SurfaceMaterial->SetScalarParameterValue(TEXT("DetailStrength"), Region.DetailFamilyCount > 0 ? FMath::Clamp(CVarSpacePlanetDetailStrength.GetValueOnGameThread(), 0.f, 1.f) : 0.f);
	}
	const float StarIntensity = FMath::Max(CVarSpaceSunIntensity.GetValueOnGameThread(), 0.f);
	for (FCelestialVisual& Celestial : Celestials)
	{
		FaceViewer(Celestial.Sprite, 0);
		if (!Celestial.StarMaterial) continue;
		Celestial.StarMaterial->SetVectorParameterValue(TEXT("StarColor"), Celestial.bMainSun ? SunStarColour : FLinearColor::White);
		Celestial.StarMaterial->SetScalarParameterValue(TEXT("Intensity"), StarIntensity);
	}
}

FString USWGSpacePlanetComponent::GetRegionCacheDirectory()
{
	return FPaths::ProjectSavedDir() / TEXT("SpacePlanetCache");
}

void USWGSpacePlanetComponent::CancelRegionBake()
{
	if (RegionCancel) *RegionCancel = true;
	// The worker only reads TRE data and posts a game-thread task that checks the cancel flag, so waiting here cannot deadlock.
	if (RegionTask.IsValid()) RegionTask.Wait();
	RegionTask = TFuture<void>(); RegionCancel.Reset();
}

void USWGSpacePlanetComponent::BeginDestroy()
{
	CancelRegionBake();
	Super::BeginDestroy();
}


float USWGSpacePlanetComponent::ComputeCapRadius(float SurfaceRadius, float ViewerDistance)
{
	// A viewer at or inside the surface sees a hemisphere; the globe maps are not meant for that, so cap it.
	if (SurfaceRadius <= 0 || ViewerDistance <= SurfaceRadius) return 1.2f;
	return FMath::Clamp(FMath::Acos(SurfaceRadius/ViewerDistance), .05f, 1.2f);
}

void USWGSpacePlanetComponent::BeginRegionBake(int32 PlanetIndex, const FString& GroundTerrain, const TArray<FString>& DetailPaths, const FSWGPlanetGlobeParams& GlobeParams,
	USWGTreSubsystem* TreSubsystem)
{
	Region.PlanetIndex = PlanetIndex; Region.GroundTerrain = GroundTerrain; Region.bPending = true;
	Region.CapRadius = GlobeParams.CapRadius; Region.Fill = GlobeParams.Fill; Region.CapResolution = GlobeParams.CapResolution; Region.CapHalfWidth = GlobeParams.GetCapHalfWidth();
	RegionCancel = MakeShared<FThreadSafeBool>(false);
	const TSharedPtr<FThreadSafeBool> Cancel = RegionCancel;
	TWeakObjectPtr<USWGSpacePlanetComponent> WeakThis(this);
	RegionTask = Async(EAsyncExecution::ThreadPool, [WeakThis, Cancel, TreSubsystem, GroundTerrain, DetailPaths, GlobeParams]()
	{
		TSharedPtr<FSWGTerrainBakeResult> Result = MakeShared<FSWGTerrainBakeResult>();
		const FString CacheDirectory = GetRegionCacheDirectory();
		// The top tier bakes the ground at its cap map's resolution (4096 = 4 m a pixel); the lower tiers stay at 1024 or less (16 m); the globe maps resample it.
		const int32 GroundResolution = GlobeParams.CapResolution > 2048 ? FMath::Min(GlobeParams.CapResolution, FSWGTerrainBake::MaxResolution)
			: FMath::Min(GlobeParams.CapResolution, FSWGTerrainBake::DefaultResolution);
		const double WorkerStart = FPlatformTime::Seconds();
		if (!FSWGTerrainBake::LoadOrBake(*TreSubsystem, GroundTerrain, GroundResolution, CacheDirectory, *Result))
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGSpacePlanetComponent: bake of %s failed; using the retail overhead map"), *GroundTerrain);
			if (!FSWGTerrainBake::LoadFallbackMap(*TreSubsystem, GroundTerrain, *Result)) Result.Reset();
		}
		TSharedPtr<FSWGPlanetGlobe> Globe;
		if (Result)
		{
			Globe = MakeShared<FSWGPlanetGlobe>();
			if (!FSWGPlanetGlobeBake::LoadOrBuild(*Result, GlobeParams, *Globe)) Globe.Reset();
		}
		// The generic detail textures' mean, so the globe can be brought to the retail globe's brightness.
		FLinearColor DetailAverage(0, 0, 0);
		int32 DetailCount = 0;
		for (const FString& DetailPath : DetailPaths)
		{
			FLinearColor Mean;
			if (FSWGTerrainBake::GetAverageColour(*TreSubsystem, DetailPath, Mean)) { DetailAverage += Mean; ++DetailCount; }
		}
		DetailAverage = DetailCount ? DetailAverage/float(DetailCount) : FLinearColor::White;
		const double WorkerSeconds = FPlatformTime::Seconds() - WorkerStart;
		if (*Cancel) return;
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Cancel, Result, Globe, DetailAverage, WorkerSeconds]()
		{
			USWGSpacePlanetComponent* Component = WeakThis.Get();
			if (!Component || *Cancel) return;
			Component->Region.bPending = false;
			Component->Region.LoadSeconds = WorkerSeconds;
			if (Result && Globe) Component->ApplyRegion(*Globe, *Result, DetailAverage);
		});
	});
}

void USWGSpacePlanetComponent::ApplyRegion(const FSWGPlanetGlobe& Globe, const FSWGTerrainBakeResult& Baked, const FLinearColor& DetailAverage)
{
	check(IsInGameThread());
	if (!Planets.IsValidIndex(Region.PlanetIndex)) return;
	FPlanetVisual& Planet = Planets[Region.PlanetIndex];
	if (!Planet.SurfaceMaterial || !IsValid(Planet.Surface)) return;
	RegionColourTexture = FSWGImageTexture::Create(Globe.CapColour, TEXT("SWGPlanetGlobeCap"), true);
	RegionNormalTexture = FSWGImageTexture::Create(Globe.CapNormal, TEXT("SWGPlanetGlobeCapRelief"), false);
	RegionFarTexture = FSWGImageTexture::Create(Globe.FarColour, TEXT("SWGPlanetGlobeFar"), true);
	RegionDetailTexture = FSWGImageTexture::Create(Globe.CapDetail, TEXT("SWGPlanetGlobeDetail"), false);
	if (!RegionColourTexture || !RegionNormalTexture || !RegionFarTexture || !RegionDetailTexture) return;

	// shortcut: the zone's launch point is not known to the scene, so the cap faces the viewer at load (the scene origin, which the sky actor follows).
	const FVector ToViewer = (GetOwner()->GetActorLocation() - Planet.Surface->GetComponentLocation()).GetSafeNormal();
	Region.Centre = Planet.Surface->GetComponentTransform().InverseTransformVectorNoScale(ToViewer).GetSafeNormal();
	Region.AverageColour = Baked.AverageColour;
	Region.bFromCache = Baked.bFromCache && Globe.bFromCache; Region.bGlobeFromCache = Globe.bFromCache; Region.bFallbackMap = Baked.bFallbackMap;
	Region.BakeSeconds = Baked.BakeSeconds; Region.GlobeSeconds = Globe.BuildSeconds;
	Region.CacheBasePath = Baked.CacheBasePath; Region.GlobeCacheBasePath = Globe.CacheBasePath;
	UMaterialInstanceDynamic* Material = Planet.SurfaceMaterial;
	Material->SetTextureParameterValue(TEXT("RegionColor"), RegionColourTexture);
	Material->SetTextureParameterValue(TEXT("RegionNormal"), RegionNormalTexture);
	Material->SetTextureParameterValue(TEXT("GlobeFar"), RegionFarTexture);
	Material->SetTextureParameterValue(TEXT("RegionDetail"), RegionDetailTexture);
	// The retail ground textures of the strongest shader families; a missing one leaves a neutral white so its slot adds nothing.
	static const TCHAR* const FamilyTextureNames[FSWGTerrainBake::DetailFamilyCount] = {TEXT("FamilyTex0"), TEXT("FamilyTex1"), TEXT("FamilyTex2"), TEXT("FamilyTex3")};
	static const TCHAR* const FamilyAverageNames[FSWGTerrainBake::DetailFamilyCount] = {TEXT("FamilyAverage0"), TEXT("FamilyAverage1"), TEXT("FamilyAverage2"), TEXT("FamilyAverage3")};
	USWGMeshGeneratorSubsystem* Meshes = MeshGeneratorRef.Get();
	UTexture* NeutralTexture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	Region.DetailFamilyCount = 0;
	for (int32 Slot = 0; Slot < FSWGTerrainBake::DetailFamilyCount; ++Slot)
	{
		UTexture2D* FamilyTexture = Meshes && Globe.DetailFamilies.IsValidIndex(Slot) ? Meshes->GetOrLoadObjectTexture(Globe.DetailFamilies[Slot].DiffusePath) : nullptr;
		if (FamilyTexture)
		{
			Material->SetTextureParameterValue(FamilyTextureNames[Slot], FamilyTexture);
			Material->SetVectorParameterValue(FamilyAverageNames[Slot], Globe.DetailFamilies[Slot].Average);
			++Region.DetailFamilyCount;
		}
		else
		{
			Material->SetTextureParameterValue(FamilyTextureNames[Slot], NeutralTexture);
			Material->SetVectorParameterValue(FamilyAverageNames[Slot], FLinearColor::White);
		}
	}
	Region.GroundPerRadian = FSWGPlanetGlobeBake::GetGroundPerRadian(Baked.MapSize > 0 ? Baked.MapSize : 16384.f, Globe.Params);
	Region.GroundResolution = Baked.Colour.Width;
	Material->SetVectorParameterValue(TEXT("RegionCentre"), FLinearColor(Region.Centre.X, Region.Centre.Y, Region.Centre.Z));
	Material->SetScalarParameterValue(TEXT("RegionCapHalfWidth"), Region.CapHalfWidth);
	Material->SetVectorParameterValue(TEXT("RegionAverage"), Baked.AverageColour);
	Material->SetVectorParameterValue(TEXT("DetailAverage"), DetailAverage);
	Material->SetScalarParameterValue(TEXT("RegionNormalStrength"), Baked.bFallbackMap ? 0.f : FMath::Clamp(CVarSpacePlanetRegionNormalStrength.GetValueOnGameThread(), 0.f, 1.f));
	Material->SetScalarParameterValue(TEXT("RegionEnabled"), 1.f);
	// The cap would turn away from the viewer with the surface's spin; the clouds keep drifting over it.
	Planet.SurfaceSpin = 0;
	Region.bReady = true;
	UE_LOG(LogTemp, Log, TEXT("USWGSpacePlanetComponent: ground bake %d px, %d detail families with textures, ground %.0f units per radian"), Region.GroundResolution, Region.DetailFamilyCount, Region.GroundPerRadian);
	UE_LOG(LogTemp, Log, TEXT("USWGSpacePlanetComponent: %s globe on planet %d, cap radius %.1f deg, fill %.2f, cap map %d, ground %s (bake %.2f s), globe %s (%.2f s), worker %.2f s"), *Region.GroundTerrain,
		Region.PlanetIndex, FMath::RadiansToDegrees(Region.CapRadius), Region.Fill, Region.CapResolution, Baked.bFromCache ? TEXT("from cache") : Baked.bFallbackMap ? TEXT("from the overhead map") : TEXT("freshly baked"),
		Baked.BakeSeconds, Globe.bFromCache ? TEXT("from cache") : TEXT("freshly built"), Globe.BuildSeconds, Region.LoadSeconds);
}
