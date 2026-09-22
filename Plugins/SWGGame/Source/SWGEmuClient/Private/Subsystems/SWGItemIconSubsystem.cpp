#include "Subsystems/SWGItemIconSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Components/SWGTangibleComponent.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TextureResource.h"

namespace
{
	TAutoConsoleVariable<bool> CVarDumpItemIcons(
		TEXT("swg.ItemIcons.Dump"), false,
		TEXT("Write every captured item icon to Saved/ItemIcons/<template crc>.png."));

	// Not the WithWorld variant: under PIE that hands over the editor world,
	// which has no icon subsystem.
	FAutoConsoleCommand CmdClearItemIcons(
		TEXT("swg.ItemIcons.Clear"),
		TEXT("Forget every cached item icon so the next inventory refresh captures them again."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (USWGItemIconSubsystem* Icons = Context.World() ? Context.World()->GetSubsystem<USWGItemIconSubsystem>() : nullptr)
				{
					Icons->ClearCache();
				}
			}
		}));

	/** The fitted ACES curve the engine's filmic tonemapper approximates. */
	float Tonemap(float Value)
	{
		return FMath::Clamp((Value * (2.51f * Value + 0.03f)) / (Value * (2.43f * Value + 0.59f) + 0.14f), 0.f, 1.f);
	}

	uint8 ToSRGB8(float Linear)
	{
		const float Encoded = Linear <= 0.0031308f ? Linear * 12.92f : 1.055f * FMath::Pow(Linear, 1.f / 2.4f) - 0.055f;
		return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Encoded, 0.f, 1.f) * 255.f));
	}

	void WritePixels(UTexture2D* Texture, const TArray<FColor>& Pixels)
	{
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
	}
}

bool USWGItemIconSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void USWGItemIconSubsystem::Deinitialize()
{
	if (Stage)
	{
		Stage->Destroy();
		Stage = nullptr;
	}
	Super::Deinitialize();
}

bool USWGItemIconSubsystem::EnsureStage()
{
	if (Stage)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Stage = World->SpawnActor<AActor>(AActor::StaticClass(), FVector(0.0, 0.0, StageHeight), FRotator::ZeroRotator, Params);
	if (!Stage)
	{
		return false;
	}

	USceneComponent* Root = NewObject<USceneComponent>(Stage, TEXT("StageRoot"));
	Stage->SetRootComponent(Root);
	Root->RegisterComponent();

	CaptureTarget = NewObject<UTextureRenderTarget2D>(this);
	CaptureTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	CaptureTarget->ClearColor = FLinearColor::Transparent;
	CaptureTarget->InitAutoFormat(IconSize, IconSize);
	CaptureTarget->UpdateResourceImmediate(true);

	// Scene colour, not final colour: it skips the tonemapper (done on
	// readback instead) but its alpha is inverse opacity, which is the only
	// way to get a transparent background out of a capture.
	Capture = NewObject<USceneCaptureComponent2D>(Stage, TEXT("IconCapture"));
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->TextureTarget = CaptureTarget;
	Capture->SetupAttachment(Root);
	Capture->RegisterComponent();

	// After registration: OnRegister resets ShowFlags from the archetype and
	// reapplies only ShowFlagSettings, so anything set earlier is lost.
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetBloom(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetAntiAliasing(true);
	// Only the stage light reaches the item, so the icon reads the same at
	// night, indoors and on every planet.
	Capture->ShowFlags.SetDirectionalLights(false);
	Capture->ShowFlags.SetSkyLighting(false);
	Capture->ShowFlags.SetSpotLights(false);
	Capture->ShowFlags.SetLumenGlobalIllumination(false);
	Capture->ShowFlags.SetLumenReflections(false);

	StageLight = NewObject<UPointLightComponent>(Stage, TEXT("StageLight"));
	StageLight->SetIntensityUnits(ELightUnits::Candelas);
	StageLight->SetAttenuationRadius(3000.f);
	StageLight->SetCastShadows(false);
	StageLight->SetupAttachment(Root);
	StageLight->RegisterComponent();

	return true;
}

UTexture2D* USWGItemIconSubsystem::MakeBlankIcon()
{
	UTexture2D* Icon = UTexture2D::CreateTransient(IconSize, IconSize, PF_B8G8R8A8);
	Icon->SRGB = true;
	Icon->NeverStream = true;

	TArray<FColor> Transparent;
	Transparent.Init(FColor(0, 0, 0, 0), IconSize * IconSize);
	WritePixels(Icon, Transparent);
	return Icon;
}

UTexture2D* USWGItemIconSubsystem::GetIcon(int64 ObjectId)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	AActor* Actor = ObjectGraph ? ObjectGraph->FindActor(ObjectId) : nullptr;
	ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(Actor);
	if (!NetObject || !MeshGenerator || NetObject->GetObjectCrc() == 0)
	{
		return nullptr;
	}

	const uint32 TemplateCrc = NetObject->GetObjectCrc();
	if (TObjectPtr<UTexture2D>* Existing = IconsByTemplateCrc.Find(TemplateCrc))
	{
		return *Existing;
	}

	if (!EnsureStage())
	{
		return nullptr;
	}

	UTexture2D* Icon = MakeBlankIcon();
	IconsByTemplateCrc.Add(TemplateCrc, Icon);

	// Volume-contained: the slot check inside RequestItemMesh is skipped, so a
	// bag item builds even though it isn't on a body.
	const USWGTangibleComponent* Tangible = Actor->FindComponentByClass<USWGTangibleComponent>();
	const FSWGCustomizationVariables Customization = Tangible ? Tangible->GetEffectiveCustomization() : FSWGCustomizationVariables();

	TWeakObjectPtr<USWGItemIconSubsystem> WeakThis(this);
	TWeakObjectPtr<UTexture2D> WeakIcon(Icon);

	MeshGenerator->RequestItemMesh(TemplateCrc, static_cast<int32>(ESWGContainmentType::VolumeContained), Customization,
		[WeakThis, WeakIcon, TemplateCrc](UStaticMesh* Mesh, const FSWGMeshData, const TArray<UMaterialInterface*>& Materials)
		{
			USWGItemIconSubsystem* Self = WeakThis.Get();
			if (!Self || !WeakIcon.IsValid() || !Mesh || !Self->EnsureStage())
			{
				return;
			}
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Self->Stage);
			Component->SetStaticMesh(Mesh);
			for (int32 Index = 0; Index < Materials.Num(); ++Index)
			{
				Component->SetMaterial(Index, Materials[Index]);
			}
			Self->CaptureMesh(Component, WeakIcon.Get(), TemplateCrc);
		},
		[WeakThis, WeakIcon, TemplateCrc](USkeletalMesh* Mesh, const FSWGMeshData, const TArray<UMaterialInterface*>& Materials)
		{
			USWGItemIconSubsystem* Self = WeakThis.Get();
			if (!Self || !WeakIcon.IsValid() || !Mesh || !Self->EnsureStage())
			{
				return;
			}
			USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Self->Stage);
			Component->SetSkeletalMesh(Mesh);
			for (int32 Index = 0; Index < Materials.Num(); ++Index)
			{
				Component->SetMaterial(Index, Materials[Index]);
			}
			Self->CaptureMesh(Component, WeakIcon.Get(), TemplateCrc);
		});

	return Icon;
}

void USWGItemIconSubsystem::CaptureMesh(UMeshComponent* Mesh, UTexture2D* Icon, uint32 TemplateCrc)
{
	// Registered now (hidden) so the stage owns it until its turn comes.
	Mesh->SetupAttachment(Stage->GetRootComponent());
	Mesh->SetCastShadow(false);
	Mesh->SetVisibility(false);
	Mesh->RegisterComponent();
	Queue.Add({ Mesh, Icon, TemplateCrc });
}

void USWGItemIconSubsystem::FinishCapture(UTexture2D* Icon, uint32 TemplateCrc)
{
	FTextureRenderTargetResource* Resource = CaptureTarget ? CaptureTarget->GameThread_GetRenderTargetResource() : nullptr;
	if (!Resource || !Icon)
	{
		return;
	}

	TArray<FLinearColor> Hdr;
	if (!Resource->ReadLinearColorPixels(Hdr) || Hdr.Num() != IconSize * IconSize)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGItemIconSubsystem: readback failed for %08X"), TemplateCrc);
		return;
	}

	// Expose, tonemap and gamma-encode as the main view would; flip the
	// inverse-opacity alpha so the empty stage is clear.
	TArray<FColor> Pixels;
	Pixels.Reserve(Hdr.Num());
	for (const FLinearColor& Source : Hdr)
	{
		FColor Pixel;
		Pixel.R = ToSRGB8(Tonemap(Source.R * IconExposure));
		Pixel.G = ToSRGB8(Tonemap(Source.G * IconExposure));
		Pixel.B = ToSRGB8(Tonemap(Source.B * IconExposure));
		Pixel.A = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(1.f - Source.A, 0.f, 1.f) * 255.f));
		Pixels.Add(Pixel);
	}
	WritePixels(Icon, Pixels);

	if (CVarDumpItemIcons.GetValueOnGameThread())
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("ItemIcons") / FString::Printf(TEXT("%08X.png"), TemplateCrc);
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(IconSize, IconSize, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		FFileHelper::SaveArrayToFile(Png, *Path);
		UE_LOG(LogTemp, Log, TEXT("USWGItemIconSubsystem: wrote %s"), *Path);
	}
}

void USWGItemIconSubsystem::Tick(float DeltaTime)
{
	// Last frame's capture has rendered by now: read it back, drop the mesh.
	if (CapturingMesh)
	{
		FinishCapture(CapturingIcon, CapturingCrc);
		CapturingMesh->DestroyComponent();
		CapturingMesh = nullptr;
		CapturingIcon = nullptr;
	}

	if (Queue.IsEmpty() || !Stage)
	{
		return;
	}

	const FPendingCapture Pending = Queue[0];
	Queue.RemoveAt(0);
	UMeshComponent* Mesh = Pending.Mesh;
	if (!Mesh || !Pending.Icon)
	{
		return;
	}

	// Centre the mesh on the stage, then look at it from front-right and
	// slightly above — enough to read a blade's edge or a helmet's face.
	USceneComponent* Root = Stage->GetRootComponent();
	Mesh->SetVisibility(true);
	const FBoxSphereBounds Bounds = Mesh->Bounds;
	const FVector StageOrigin = Root->GetComponentLocation();
	Mesh->SetWorldLocation(StageOrigin - (Bounds.Origin - Mesh->GetComponentLocation()));

	const float Radius = FMath::Max(Bounds.SphereRadius, 1.f);
	const FRotator ViewRotation(-25.f, 135.f, 0.f);
	Capture->SetWorldLocationAndRotation(StageOrigin - ViewRotation.Vector() * Radius * 4.f, ViewRotation);
	Capture->OrthoWidth = Radius * 2.2f;
	// A few lux is a well-lit surface at IconExposure; scaled to the item's size.
	const float LightDistance = Radius * 3.f;
	StageLight->SetWorldLocation(StageOrigin - ViewRotation.Vector() * LightDistance + FVector(0.f, 0.f, Radius));
	StageLight->SetIntensity(StageLightBrightness * FMath::Square(LightDistance / 100.f));

	Capture->ClearShowOnlyComponents();
	Capture->ShowOnlyComponent(Mesh);

	// Deferred: rendered with this frame's scene, which gives the render
	// thread its time context. An immediate CaptureScene() from here ensures.
	Capture->CaptureSceneDeferred();
	CapturingMesh = Mesh;
	CapturingIcon = Pending.Icon;
	CapturingCrc = Pending.TemplateCrc;

	UE_LOG(LogTemp, Verbose, TEXT("USWGItemIconSubsystem: capturing %08X - radius %.1f"), Pending.TemplateCrc, Radius);
}

void USWGItemIconSubsystem::ClearCache()
{
	IconsByTemplateCrc.Empty();
	ModelsByTemplateCrc.Empty();
	Queue.Reset();
	CapturingMesh = nullptr;
	CapturingIcon = nullptr;

	// The stage goes too (its meshes with it), so the next capture rebuilds
	// it — which is what makes this useful while tuning the capture setup.
	if (Stage)
	{
		Stage->Destroy();
		Stage = nullptr;
		Capture = nullptr;
		StageLight = nullptr;
		CaptureTarget = nullptr;
	}
}

void USWGItemIconSubsystem::RequestItemModel(int64 ObjectId, TFunction<void(UObject*, const TArray<UMaterialInterface*>&)> OnReady)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	AActor* Actor = ObjectGraph ? ObjectGraph->FindActor(ObjectId) : nullptr;
	ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(Actor);
	if (!NetObject || NetObject->GetObjectCrc() == 0)
	{
		// No spawned actor — true for ITNO/intangible objects (datapad
		// contents) by design, see SWGFormTagMappings.csv's SITN row. Their
		// template CRC is still recorded in USWGObjectGraphSubsystem
		// (HandleSceneCreateObject) even though nothing spawned for it, so a
		// preview can still be built the same way RequestModelForTemplateCrc
		// does — just with no live customization to read.
		if (const uint32 Crc = ObjectGraph ? ObjectGraph->FindObjectCrc(ObjectId) : 0; Crc != 0 && OnReady)
		{
			RequestModelForTemplateCrcImpl(Crc, FSWGCustomizationVariables(), MoveTemp(OnReady));
		}
		return;
	}
	if (!OnReady)
	{
		return;
	}

	// Volume-contained: the slot check inside RequestItemMesh is skipped, so a
	// bag item builds even though it isn't on a body.
	const USWGTangibleComponent* Tangible = Actor->FindComponentByClass<USWGTangibleComponent>();
	const FSWGCustomizationVariables Customization = Tangible ? Tangible->GetEffectiveCustomization() : FSWGCustomizationVariables();

	RequestModelForTemplateCrcImpl(NetObject->GetObjectCrc(), Customization, MoveTemp(OnReady));
}

void USWGItemIconSubsystem::RequestModelForTemplateCrc(uint32 TemplateCrc, TFunction<void(UObject*, const TArray<UMaterialInterface*>&)> OnReady)
{
	RequestModelForTemplateCrcImpl(TemplateCrc, FSWGCustomizationVariables(), MoveTemp(OnReady));
}

void USWGItemIconSubsystem::RequestModelForTemplateCrcImpl(uint32 TemplateCrc, const FSWGCustomizationVariables& Customization, TFunction<void(UObject*, const TArray<UMaterialInterface*>&)> OnReady)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	if (!MeshGenerator || TemplateCrc == 0 || !OnReady)
	{
		return;
	}

	if (const FSWGCachedItemModel* Cached = ModelsByTemplateCrc.Find(TemplateCrc))
	{
		TArray<UMaterialInterface*> Materials;
		for (const TObjectPtr<UMaterialInterface>& Material : Cached->Materials)
		{
			Materials.Add(Material);
		}
		OnReady(Cached->Mesh, Materials);
		return;
	}

	TArray<TFunction<void(UObject*, const TArray<UMaterialInterface*>&)>>& Waiting = PendingModelRequests.FindOrAdd(TemplateCrc);
	Waiting.Add(MoveTemp(OnReady));
	if (Waiting.Num() > 1)
	{
		// A build is already in flight for this template.
		return;
	}

	TWeakObjectPtr<USWGItemIconSubsystem> WeakThis(this);
	const auto Deliver = [WeakThis, TemplateCrc](UObject* Mesh, const TArray<UMaterialInterface*>& Materials)
	{
		USWGItemIconSubsystem* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}

		TArray<TFunction<void(UObject*, const TArray<UMaterialInterface*>&)>> Waiting;
		Self->PendingModelRequests.RemoveAndCopyValue(TemplateCrc, Waiting);
		UE_LOG(LogTemp, Log, TEXT("USWGItemIconSubsystem: model for %08X -> %s (%d material(s), %d waiting)"),
			TemplateCrc, Mesh ? *Mesh->GetName() : TEXT("none"), Materials.Num(), Waiting.Num());
		if (!Mesh)
		{
			return;
		}

		FSWGCachedItemModel& Cached = Self->ModelsByTemplateCrc.Add(TemplateCrc);
		Cached.Mesh = Mesh;
		for (UMaterialInterface* Material : Materials)
		{
			Cached.Materials.Add(Material);
		}
		for (const TFunction<void(UObject*, const TArray<UMaterialInterface*>&)>& Callback : Waiting)
		{
			Callback(Mesh, Materials);
		}
	};

	MeshGenerator->RequestItemMesh(TemplateCrc, static_cast<int32>(ESWGContainmentType::VolumeContained), Customization,
		[Deliver](UStaticMesh* Mesh, const FSWGMeshData, const TArray<UMaterialInterface*>& Materials) { Deliver(Mesh, Materials); },
		[Deliver](USkeletalMesh* Mesh, const FSWGMeshData, const TArray<UMaterialInterface*>& Materials) { Deliver(Mesh, Materials); });
}
