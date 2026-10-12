#include "SWGEffectsSubsystem.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "SWGAudioSubsystem.h"
#include "SWGEffects.h"
#include "SWGParticleEffectComponent.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGShaderReader.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMaxParticles(TEXT("swg.Fx.MaxParticles"), 6000, TEXT("Global live-particle budget; new effects are refused above it."));
	TAutoConsoleVariable<int32> CVarMaxParticlesPerEmitter(TEXT("swg.Fx.MaxParticlesPerEmitter"), 1024, TEXT("Hard cap on live particles of one emitter."));
	TAutoConsoleVariable<float> CVarCullDistance(TEXT("swg.Fx.CullDistance"), 150.f, TEXT("Effects further than this many metres from the camera are hidden and frozen (0 = never)."));
	TAutoConsoleVariable<int32> CVarMaxSkeletalParticles(TEXT("swg.Fx.MaxSkeletalParticles"), 24, TEXT("Most .sat mesh particles (ambient animals) drawn at once; the rest are skipped."));
	TAutoConsoleVariable<int32> CVarForceWorldSpace(TEXT("swg.Fx.ForceWorldSpace"), 0, TEXT("1 = particles of every emitter stay in the world after spawning (default: unless the emitter is local-space)."));
	TAutoConsoleVariable<float> CVarQuadRotationUnit(TEXT("swg.Fx.QuadRotationRadiansPerUnit"), UE_TWO_PI, TEXT("Radians per unit of the PTQD rotation curve (retail: 1 turn)."));

	/** Names of the generated masters (SWGDevToolset swg.CreateFxMaterials), indexed by ESWGEffectBlendMode. */
	const TCHAR* MasterName(ESWGEffectBlendMode Mode)
	{
		switch (Mode)
		{
			case ESWGEffectBlendMode::Additive: return TEXT("M_SWGParticle_Additive");
			case ESWGEffectBlendMode::Masked: return TEXT("M_SWGParticle_Masked");
			case ESWGEffectBlendMode::Modulate: return TEXT("M_SWGParticle_Modulate");
			case ESWGEffectBlendMode::Subtractive: return TEXT("M_SWGParticle_Subtractive");
			default: return TEXT("M_SWGParticle_Translucent");
		}
	}

	/** Diffuse texture path and blend mode of a particle shader; animated shaders use their first frame and base shader. */
	bool ResolveShader(const USWGTreSubsystem& Tre, const FString& ShaderPath, FString& OutTexturePath, ESWGEffectBlendMode& OutBlend, int32 Depth = 0)
	{
		if (Depth > 2 || ShaderPath.IsEmpty() || !Tre.FileExists(ShaderPath))
		{
			return false;
		}

		const FSWGIffReader ShaderReader = Tre.CreateIffReader(ShaderPath);
		FSWGShaderData Shader;
		if (!FSWGShaderReader::ReadShader(ShaderReader, Shader))
		{
			FSWGShaderAnimation Animation;
			if (!FSWGShaderReader::ReadAnimation(ShaderReader, Animation))
			{
				return false;
			}
			if (Animation.bSwitchesShaders)
			{
				return !Animation.Shaders.IsEmpty() && ResolveShader(Tre, Animation.Shaders[0].Replace(TEXT("\\"), TEXT("/")), OutTexturePath, OutBlend, Depth + 1);
			}
			const bool bBaseOk = ResolveShader(Tre, Animation.BaseShaderPath.Replace(TEXT("\\"), TEXT("/")), OutTexturePath, OutBlend, Depth + 1);
			if (!Animation.Textures.IsEmpty())
			{
				OutTexturePath = Animation.Textures[0].TexturePath.Replace(TEXT("\\"), TEXT("/"));
			}
			return bBaseOk || !OutTexturePath.IsEmpty();
		}

		const FSWGShaderTexture* Main = Shader.FindTexture(ESWGShaderTextureUsage::Diffuse);
		if (!Main && !Shader.Textures.IsEmpty())
		{
			Main = &Shader.Textures[0];
		}
		OutTexturePath = Main ? Main->VirtualPath.Replace(TEXT("\\"), TEXT("/")) : FString();

		FSWGEffectData Effect;
		const FString EffectPath = Shader.EffectName.Replace(TEXT("\\"), TEXT("/"));
		const bool bEffectRead = Shader.bHasInlineEffect ? FSWGShaderReader::ReadEffect(ShaderReader, Effect)
			: (!EffectPath.IsEmpty() && Tre.FileExists(EffectPath) && FSWGShaderReader::ReadEffect(Tre.CreateIffReader(EffectPath), Effect));
		OutBlend = bEffectRead ? Effect.GetBlendMode() : (Shader.NeedsAlphaBlend() ? ESWGEffectBlendMode::Translucent : ESWGEffectBlendMode::Opaque);
		return true;
	}
}

bool FSWGEffectHandle::IsValid() const
{
	return Effect.IsValid() && Effect->GetSerial() == Serial;
}

bool FSWGEffectHandle::IsActive() const
{
	return IsValid() && Effect->IsEffectActive();
}

void FSWGEffectHandle::Stop(bool bImmediate) const
{
	if (IsValid())
	{
		Effect->StopEffect(bImmediate);
	}
}

bool USWGEffectsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void USWGEffectsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void USWGEffectsSubsystem::Deinitialize()
{
	StopAll(true);
	ActiveEffects.Reset();
	FreeEffects.Reset();
	FreeInstanceComponents.Reset();
	FreeMeshComponents.Reset();
	FreeSkeletalComponents.Reset();
	PendingSkeletalComponents.Reset();
	OrphanedSkeletalComponents.Reset();
	SkeletalInUse = 0;
	ParticleMaterials.Reset();
	RibbonMaterials.Reset();
	RibbonMasters.Reset();
	Host = nullptr;
	Super::Deinitialize();
}

TStatId USWGEffectsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGEffectsSubsystem, STATGROUP_Tickables);
}

USWGAudioSubsystem* USWGEffectsSubsystem::GetAudioSubsystem() const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<USWGAudioSubsystem>() : nullptr;
}

USWGTreSubsystem* USWGEffectsSubsystem::GetTreSubsystem() const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
}

FSWGEffectSimSettings USWGEffectsSubsystem::MakeSimSettings() const
{
	FSWGEffectSimSettings Settings;
	Settings.TerrainHeight = TerrainHeightProvider;
	Settings.MaxParticlesPerEmitter = FMath::Max(1, CVarMaxParticlesPerEmitter.GetValueOnGameThread());
	Settings.bForceWorldSpace = CVarForceWorldSpace.GetValueOnGameThread() != 0;
	Settings.QuadRotationRadiansPerUnit = CVarQuadRotationUnit.GetValueOnGameThread();
	return Settings;
}

FString USWGEffectsSubsystem::NormalizeEffectPath(const FString& PrtPath)
{
	FString Path = PrtPath.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/")).ToLower();
	if (!Path.EndsWith(TEXT(".prt")))
	{
		Path += TEXT(".prt");
	}
	if (!Path.Contains(TEXT("/")))
	{
		Path = TEXT("appearance/") + Path;
	}
	return Path;
}

TSharedPtr<const FSWGParticleEffect> USWGEffectsSubsystem::GetOrLoadEffect(const FString& PrtPath)
{
	const FString Path = NormalizeEffectPath(PrtPath);
	if (const TSharedPtr<const FSWGParticleEffect>* Cached = EffectCache.Find(Path))
	{
		return *Cached;
	}

	TSharedPtr<const FSWGParticleEffect> Result;
	const USWGTreSubsystem* Tre = GetTreSubsystem();
	if (Tre && Tre->FileExists(Path))
	{
		const FSWGIffReader Reader = Tre->CreateIffReader(Path);
		if (Reader.IsValid())
		{
			TSharedRef<FSWGParticleEffect> Parsed = MakeShared<FSWGParticleEffect>();
			FString Error;
			if (FSWGParticleEffectReader::Read(Reader, *Parsed, &Error))
			{
				Result = Parsed;
			}
			else
			{
				UE_LOG(LogSWGFx, Warning, TEXT("Could not parse %s: %s"), *Path, *Error);
			}
		}
	}
	else
	{
		UE_LOG(LogSWGFx, Warning, TEXT("Particle effect %s not found"), *Path);
	}
	EffectCache.Add(Path, Result);
	return Result;
}

AActor* USWGEffectsSubsystem::GetOrCreateHost()
{
	if (IsValid(Host))
	{
		return Host;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Host = GetWorld()->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	USceneComponent* Root = NewObject<USceneComponent>(Host, TEXT("Root"), RF_Transient);
	Root->SetMobility(EComponentMobility::Movable);
	Host->SetRootComponent(Root);
	Root->RegisterComponent();
	Host->SetActorTickEnabled(false);
#if WITH_EDITOR
	Host->SetActorLabel(TEXT("SWGEffectsHost"));
#endif
	return Host;
}

UStaticMesh* USWGEffectsSubsystem::GetQuadMesh()
{
	if (QuadMesh)
	{
		return QuadMesh;
	}

	// A 100 cm quad in the XY plane. UV (0,0) is the top-left: V falls as local Y rises, which the billboard WPO relies on.
	FMeshDescription Description;
	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	UVs.SetNumChannels(1);

	const FVector3f Corners[4] = {FVector3f(-50.f, -50.f, 0.f), FVector3f(50.f, -50.f, 0.f), FVector3f(50.f, 50.f, 0.f), FVector3f(-50.f, 50.f, 0.f)};
	const FVector2f CornerUVs[4] = {FVector2f(0.f, 1.f), FVector2f(1.f, 1.f), FVector2f(1.f, 0.f), FVector2f(0.f, 0.f)};
	const FPolygonGroupID Group = Description.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Particle");

	TArray<FVertexInstanceID> CornerInstances;
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const FVertexID Vertex = Description.CreateVertex();
		Positions[Vertex] = Corners[Corner];
		const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
		Normals[Instance] = FVector3f(0.f, 0.f, 1.f);
		Tangents[Instance] = FVector3f(1.f, 0.f, 0.f);
		BinormalSigns[Instance] = 1.f;
		UVs.Set(Instance, 0, CornerUVs[Corner]);
		CornerInstances.Add(Instance);
	}
	Description.CreatePolygon(Group, CornerInstances);

	UStaticMesh* Mesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), TEXT("Particle")));
	UStaticMesh::FBuildMeshDescriptionsParams BuildParameters;
	BuildParameters.bMarkPackageDirty = false;
	BuildParameters.bBuildSimpleCollision = false;
	BuildParameters.bFastBuild = true;
	Mesh->BuildFromMeshDescriptions({&Description}, BuildParameters);
	QuadMesh = Mesh;
	return QuadMesh;
}

UStaticMesh* USWGEffectsSubsystem::GetBeamMesh()
{
	if (BeamMesh)
	{
		return BeamMesh;
	}

	// Retail BeamAppearance draws (+-width, 0, 0..length) with U 0 at the far end and 1 at the origin, V 0 at +x and 1 at -x.
	FMeshDescription Description;
	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	UVs.SetNumChannels(2);

	const FVector3f Corners[4] = {FVector3f(50.f, 0.f, 0.f), FVector3f(50.f, 0.f, 100.f), FVector3f(-50.f, 0.f, 100.f), FVector3f(-50.f, 0.f, 0.f)};
	const FVector2f CornerUVs[4] = {FVector2f(1.f, 0.f), FVector2f(0.f, 0.f), FVector2f(0.f, 1.f), FVector2f(1.f, 1.f)};
	const FPolygonGroupID Group = Description.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Beam");

	TArray<FVertexInstanceID> CornerInstances;
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const FVertexID Vertex = Description.CreateVertex();
		Positions[Vertex] = Corners[Corner];
		const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
		Normals[Instance] = FVector3f(0.f, -1.f, 0.f);
		Tangents[Instance] = FVector3f(1.f, 0.f, 0.f);
		BinormalSigns[Instance] = 1.f;
		UVs.Set(Instance, 0, CornerUVs[Corner]);
		UVs.Set(Instance, 1, FVector2f(1.f, 0.f));
		CornerInstances.Add(Instance);
	}
	Description.CreatePolygon(Group, CornerInstances);

	UStaticMesh* Mesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), TEXT("Beam")));
	UStaticMesh::FBuildMeshDescriptionsParams BuildParameters;
	BuildParameters.bMarkPackageDirty = false;
	BuildParameters.bBuildSimpleCollision = false;
	BuildParameters.bFastBuild = true;
	Mesh->BuildFromMeshDescriptions({&Description}, BuildParameters);
	BeamMesh = Mesh;
	return BeamMesh;
}

UInstancedStaticMeshComponent* USWGEffectsSubsystem::AcquireInstanceComponent()
{
	UInstancedStaticMeshComponent* Component = nullptr;
	while (!FreeInstanceComponents.IsEmpty() && !IsValid(Component))
	{
		Component = FreeInstanceComponents.Pop();
	}

	if (!IsValid(Component))
	{
		AActor* HostActor = GetOrCreateHost();
		Component = NewObject<UInstancedStaticMeshComponent>(HostActor, NAME_None, RF_Transient);
		Component->SetStaticMesh(GetQuadMesh());
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetNumCustomDataFloats(USWGParticleEffectComponent::CustomDataFloats);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetGenerateOverlapEvents(false);
		Component->bAffectDynamicIndirectLighting = false;
		Component->bAffectDistanceFieldLighting = false;
		Component->SetupAttachment(HostActor->GetRootComponent());
		Component->RegisterComponent();
	}
	Component->SetVisibility(true);
	return Component;
}

void USWGEffectsSubsystem::ReleaseInstanceComponent(UInstancedStaticMeshComponent* Component)
{
	if (IsValid(Component))
	{
		Component->ClearInstances();
		Component->SetVisibility(false);
		FreeInstanceComponents.Add(Component);
	}
}

UStaticMeshComponent* USWGEffectsSubsystem::AcquireMeshComponent()
{
	UStaticMeshComponent* Component = nullptr;
	while (!FreeMeshComponents.IsEmpty() && !IsValid(Component))
	{
		Component = FreeMeshComponents.Pop();
	}

	if (!IsValid(Component))
	{
		AActor* HostActor = GetOrCreateHost();
		Component = NewObject<UStaticMeshComponent>(HostActor, NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetGenerateOverlapEvents(false);
		Component->SetupAttachment(HostActor->GetRootComponent());
		Component->RegisterComponent();
	}
	Component->SetVisibility(true);
	return Component;
}

void USWGEffectsSubsystem::ReleaseMeshComponent(UStaticMeshComponent* Component)
{
	if (IsValid(Component))
	{
		Component->SetVisibility(false);
		Component->SetStaticMesh(nullptr);
		FreeMeshComponents.Add(Component);
	}
}

USkeletalMeshComponent* USWGEffectsSubsystem::AcquireSkeletalComponent()
{
	if (SkeletalInUse >= CVarMaxSkeletalParticles.GetValueOnGameThread() || !SkeletalMeshProvider)
	{
		return nullptr;
	}

	USkeletalMeshComponent* Component = nullptr;
	while (!FreeSkeletalComponents.IsEmpty() && !IsValid(Component))
	{
		Component = FreeSkeletalComponents.Pop();
	}

	if (!IsValid(Component))
	{
		AActor* HostActor = GetOrCreateHost();
		Component = NewObject<USkeletalMeshComponent>(HostActor, NAME_None, RF_Transient);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetGenerateOverlapEvents(false);
		Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Component->SetupAttachment(HostActor->GetRootComponent());
		Component->RegisterComponent();
	}
	// The caller shows it once the mesh and loop are in.
	Component->SetVisibility(false);
	Component->SetComponentTickEnabled(true);
	++SkeletalInUse;
	return Component;
}

void USWGEffectsSubsystem::ReleaseSkeletalComponent(USkeletalMeshComponent* Component)
{
	if (!IsValid(Component))
	{
		return;
	}
	if (PendingSkeletalComponents.Contains(Component))
	{
		OrphanedSkeletalComponents.Add(Component);
		return;
	}
	Component->SetVisibility(false);
	Component->SetComponentTickEnabled(false);
	FreeSkeletalComponents.Add(Component);
	SkeletalInUse = FMath::Max(0, SkeletalInUse - 1);
}

void USWGEffectsSubsystem::RequestSkeletalParticle(const FString& SatPath, USkeletalMeshComponent& Component, TFunction<void(bool)> OnReady)
{
	if (!SkeletalMeshProvider)
	{
		OnReady(false);
		return;
	}

	PendingSkeletalComponents.Add(&Component);
	TWeakObjectPtr<USWGEffectsSubsystem> WeakThis(this);
	TWeakObjectPtr<USkeletalMeshComponent> WeakComponent(&Component);
	auto Finish = [WeakThis, WeakComponent, OnReady = MoveTemp(OnReady)](bool bPlaying)
	{
		USWGEffectsSubsystem* Self = WeakThis.Get();
		USkeletalMeshComponent* Mesh = WeakComponent.Get();
		if (!Self || !Mesh)
		{
			return;
		}
		Self->PendingSkeletalComponents.Remove(Mesh);
		// Released while building: back to the pool now that the request can no longer touch it.
		if (Self->OrphanedSkeletalComponents.Remove(Mesh) > 0)
		{
			Self->ReleaseSkeletalComponent(Mesh);
			return;
		}
		OnReady(bPlaying);
	};

	SkeletalMeshProvider(SatPath, Component, [Finish](bool bPlaying)
	{
		if (IsInGameThread())
		{
			Finish(bPlaying);
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [Finish, bPlaying]() { Finish(bPlaying); });
		}
	});
}

USWGParticleEffectComponent* USWGEffectsSubsystem::AcquireEffect()
{
	USWGParticleEffectComponent* Effect = nullptr;
	while (!FreeEffects.IsEmpty() && !IsValid(Effect))
	{
		Effect = FreeEffects.Pop();
	}

	if (!IsValid(Effect))
	{
		AActor* HostActor = GetOrCreateHost();
		Effect = NewObject<USWGParticleEffectComponent>(HostActor, NAME_None, RF_Transient);
		Effect->SetMobility(EComponentMobility::Movable);
		Effect->SetupAttachment(HostActor->GetRootComponent());
		Effect->RegisterComponent();
	}
	return Effect;
}

void USWGEffectsSubsystem::RecycleEffect(USWGParticleEffectComponent* Effect)
{
	if (!IsValid(Effect))
	{
		return;
	}
	Effect->ReleaseResources();
	if (IsValid(Host) && Host->GetRootComponent())
	{
		Effect->AttachToComponent(Host->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	}
	FreeEffects.Add(Effect);
}

bool USWGEffectsSubsystem::GetCameraLocation(FVector& OutLocation) const
{
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (!Camera)
	{
		return false;
	}
	OutLocation = Camera->GetCameraLocation();
	return true;
}

int32 USWGEffectsSubsystem::CountLiveParticles() const
{
	int32 Total = 0;
	for (const USWGParticleEffectComponent* Effect : ActiveEffects)
	{
		Total += IsValid(Effect) ? Effect->GetLiveParticleCount() : 0;
	}
	return Total;
}

FSWGEffectHandle USWGEffectsSubsystem::PlayParticleAttached(const FString& PrtPath, USceneComponent* Parent, FName SocketName, float Scale, const FLinearColor& Tint, const FTransform& LocalTransform)
{
	if (!IsValid(Parent))
	{
		return FSWGEffectHandle();
	}
	FSWGEffectPlayParams Params;
	Params.Scale = Scale;
	Params.Tint = Tint;
	return PlayParticle(PrtPath, Parent, SocketName, LocalTransform * Parent->GetSocketTransform(SocketName), Params);
}

FSWGEffectHandle USWGEffectsSubsystem::PlayParticleAtLocation(const FString& PrtPath, const FTransform& WorldTransform, float Scale, const FLinearColor& Tint)
{
	FSWGEffectPlayParams Params;
	Params.Scale = Scale;
	Params.Tint = Tint;
	return PlayParticle(PrtPath, nullptr, NAME_None, WorldTransform, Params);
}

FSWGEffectHandle USWGEffectsSubsystem::PlayParticle(const FString& PrtPath, USceneComponent* AttachParent, FName SocketName, const FTransform& WorldTransform, const FSWGEffectPlayParams& Params)
{
	const TSharedPtr<const FSWGParticleEffect> Effect = GetOrLoadEffect(PrtPath);
	if (!Effect.IsValid() || !GetWorld())
	{
		return FSWGEffectHandle();
	}

	FVector CameraLocation;
	const float CullMeters = CVarCullDistance.GetValueOnGameThread();
	const bool bTooFar = CullMeters > 0.f && GetCameraLocation(CameraLocation) && FVector::Dist(CameraLocation, WorldTransform.GetLocation()) > CullMeters * SWGWorldScale * 1.5f;
	if (bTooFar || CountLiveParticles() >= CVarMaxParticles.GetValueOnGameThread())
	{
		++RejectedPlays;
		return FSWGEffectHandle();
	}

	USWGParticleEffectComponent* Component = AcquireEffect();
	if (AttachParent)
	{
		Component->AttachToComponent(AttachParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
		Component->SetWorldLocationAndRotation(WorldTransform.GetLocation(), WorldTransform.GetRotation());
		Component->bHasAttachParent = true;
		Component->AttachParentWatch = AttachParent;
	}
	else
	{
		Component->bHasAttachParent = false;
		Component->AttachParentWatch.Reset();
		Component->AttachToComponent(GetOrCreateHost()->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		Component->SetWorldTransform(FTransform(WorldTransform.GetRotation(), WorldTransform.GetLocation(), FVector::OneVector));
	}

	FSWGEffectPlayParams ResolvedParams = Params;
	if (ResolvedParams.Seed == 0)
	{
		ResolvedParams.Seed = HashCombineFast(NextAutoSeed++, 0x9e3779b9u);
	}
	const uint32 Serial = NextSerial++;
	Component->BeginEffect(*this, NormalizeEffectPath(PrtPath), Effect.ToSharedRef(), ResolvedParams, Serial);
	ActiveEffects.Add(Component);

	FSWGEffectHandle Handle;
	Handle.Effect = Component;
	Handle.Serial = Serial;
	return Handle;
}

void USWGEffectsSubsystem::StopAll(bool bImmediate)
{
	for (USWGParticleEffectComponent* Effect : ActiveEffects)
	{
		if (IsValid(Effect))
		{
			Effect->StopEffect(bImmediate);
		}
	}
}

void USWGEffectsSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	FVector CameraLocation = FVector::ZeroVector;
	const bool bHasCamera = GetCameraLocation(CameraLocation);
	const float CullCentimeters = CVarCullDistance.GetValueOnGameThread() * SWGWorldScale;
	const float StepSeconds = FMath::Min(DeltaTime, 0.25f);

	CulledCount = 0;
	// Index order: a child effect played during a tick lands at the end and is simply ticked next frame.
	for (int32 Index = ActiveEffects.Num() - 1; Index >= 0; --Index)
	{
		USWGParticleEffectComponent* Effect = ActiveEffects[Index];
		if (!IsValid(Effect))
		{
			ActiveEffects.RemoveAtSwap(Index);
			continue;
		}

		if (Effect->bHasAttachParent && !Effect->AttachParentWatch.IsValid())
		{
			Effect->bHasAttachParent = false;
			Effect->StopEffect(false);
		}

		const bool bCulled = bHasCamera && CullCentimeters > 0.f && FVector::Dist(CameraLocation, Effect->GetComponentLocation()) > CullCentimeters;
		CulledCount += bCulled ? 1 : 0;
		Effect->TickEffect(StepSeconds, bCulled);
		if (Effect->GetSecondsCulled() > 5.f)
		{
			Effect->StopEffect(true);
		}

		if (Effect->IsEffectFinished())
		{
			ActiveEffects.RemoveAtSwap(Index);
			RecycleEffect(Effect);
		}
	}
}

FSWGEffectsStats USWGEffectsSubsystem::GetStats() const
{
	FSWGEffectsStats Stats;
	Stats.ActiveEffects = ActiveEffects.Num();
	Stats.CulledEffects = CulledCount;
	Stats.LiveParticles = CountLiveParticles();
	Stats.PooledEffects = FreeEffects.Num();
	Stats.PooledInstanceComponents = FreeInstanceComponents.Num();
	Stats.PooledMeshComponents = FreeMeshComponents.Num();
	Stats.SkeletalParticles = SkeletalInUse;
	Stats.CachedEffects = EffectCache.Num();
	Stats.CachedMaterials = ParticleMaterials.Num();
	Stats.RejectedPlays = RejectedPlays;
	return Stats;
}

UMaterialInterface* USWGEffectsSubsystem::LoadParticleMaster(int32 BlendModeIndex)
{
	if (const TObjectPtr<UMaterialInterface>* Cached = MasterMaterials.Find(BlendModeIndex))
	{
		return *Cached;
	}

	const FString Name = MasterName((ESWGEffectBlendMode)BlendModeIndex);
	UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/SWGEmu/Materials/%s.%s"), *Name, *Name));
	if (!Master)
	{
		if (!bWarnedMissingMasters)
		{
			bWarnedMissingMasters = true;
			UE_LOG(LogSWGFx, Error, TEXT("Particle master %s is missing - run swg.CreateFxMaterials in the editor; particles fall back to the default material"), *Name);
		}
		Master = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	MasterMaterials.Add(BlendModeIndex, Master);
	return Master;
}

UMaterialInterface* USWGEffectsSubsystem::GetParticleMaterial(const FString& ShaderPath, int32 GridColumns)
{
	const int32 Columns = FMath::Max(GridColumns, 1);
	const FString Key = FString::Printf(TEXT("%s|%d"), *ShaderPath.ToLower(), Columns);
	if (const TObjectPtr<UMaterialInterface>* Cached = ParticleMaterials.Find(Key))
	{
		return *Cached;
	}

	FString TexturePath;
	ESWGEffectBlendMode Blend = ESWGEffectBlendMode::Translucent;
	USWGTreSubsystem* Tre = GetTreSubsystem();
	const bool bResolved = Tre && ResolveShader(*Tre, ShaderPath.Replace(TEXT("\\"), TEXT("/")), TexturePath, Blend);
	if (!bResolved)
	{
		UE_LOG(LogSWGFx, Warning, TEXT("Particle shader %s did not resolve; using a plain translucent quad"), *ShaderPath);
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(LoadParticleMaster((int32)Blend), this);
	if (Tre && !TexturePath.IsEmpty())
	{
		if (UTexture2D* Texture = Tre->GetOrLoadTexture(TexturePath, true))
		{
			Instance->SetTextureParameterValue(TEXT("Texture"), Texture);
		}
	}
	Instance->SetScalarParameterValue(TEXT("GridColumns"), (float)Columns);
	ParticleMaterials.Add(Key, Instance);
	return Instance;
}

UMaterialInterface* USWGEffectsSubsystem::BuildRibbonMaterial(const FString& ShaderPath)
{
	const FString Key = ShaderPath.ToLower();
	if (const TObjectPtr<UMaterialInterface>* Cached = RibbonMaterials.Find(Key))
	{
		return *Cached;
	}

	FString TexturePath;
	ESWGEffectBlendMode Blend = ESWGEffectBlendMode::Translucent;
	USWGTreSubsystem* Tre = GetTreSubsystem();
	if (!Tre || !ResolveShader(*Tre, ShaderPath, TexturePath, Blend))
	{
		UE_LOG(LogSWGFx, Warning, TEXT("Ribbon shader %s did not resolve; using a plain translucent strip"), *ShaderPath);
	}

	const bool bAdditive = Blend == ESWGEffectBlendMode::Additive;
	UMaterialInterface* Master = nullptr;
	if (const TObjectPtr<UMaterialInterface>* CachedMaster = RibbonMasters.Find(bAdditive ? 1 : 0))
	{
		Master = *CachedMaster;
	}
	else
	{
		const TCHAR* Name = bAdditive ? TEXT("M_SWGRibbon_Additive") : TEXT("M_SWGRibbon_Translucent");
		Master = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/SWGEmu/Materials/%s.%s"), Name, Name));
		if (!Master)
		{
			UE_LOG(LogSWGFx, Error, TEXT("Ribbon master %s is missing - run swg.CreateFxMaterials in the editor"), Name);
			Master = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		RibbonMasters.Add(bAdditive ? 1 : 0, Master);
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Master, this);
	if (Tre && !TexturePath.IsEmpty())
	{
		if (UTexture2D* Texture = Tre->GetOrLoadTexture(TexturePath, true))
		{
			Instance->SetTextureParameterValue(TEXT("Texture"), Texture);
		}
	}
	RibbonMaterials.Add(Key, Instance);
	return Instance;
}

int32 USWGEffectsSubsystem::GetRibbonMaterials(const FString& ShaderPath, TArray<UMaterialInterface*>& OutMaterials, float& OutSecondsPerFrame)
{
	OutMaterials.Reset();
	OutSecondsPerFrame = 0.f;
	const FString Path = ShaderPath.Replace(TEXT("\\"), TEXT("/"));
	USWGTreSubsystem* Tre = GetTreSubsystem();
	if (!Tre || Path.IsEmpty() || !Tre->FileExists(Path))
	{
		return 0;
	}

	const FSWGIffReader ShaderReader = Tre->CreateIffReader(Path);
	FSWGShaderData Shader;
	FSWGShaderAnimation Animation;
	if (!FSWGShaderReader::ReadShader(ShaderReader, Shader) && FSWGShaderReader::ReadAnimation(ShaderReader, Animation) && Animation.bSwitchesShaders)
	{
		for (const FString& FrameShader : Animation.Shaders)
		{
			OutMaterials.Add(BuildRibbonMaterial(FrameShader.Replace(TEXT("\\"), TEXT("/"))));
		}
		OutSecondsPerFrame = Animation.SecondsPerFrame;
		return OutMaterials.Num();
	}

	OutMaterials.Add(BuildRibbonMaterial(Path));
	return 1;
}

void USWGEffectsSubsystem::RequestParticleMesh(const FString& MeshPath, TFunction<void(UStaticMesh*, const TArray<UMaterialInterface*>&)> OnComplete)
{
	if (!StaticMeshProvider)
	{
		OnComplete(nullptr, TArray<UMaterialInterface*>());
		return;
	}

	// The provider reports on the game thread, but continuations upstream have been seen off it, so hop to be safe.
	StaticMeshProvider(MeshPath, [OnComplete = MoveTemp(OnComplete)](UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
	{
		if (IsInGameThread())
		{
			OnComplete(Mesh, Materials);
			return;
		}
		TWeakObjectPtr<UStaticMesh> WeakMesh(Mesh);
		TArray<TWeakObjectPtr<UMaterialInterface>> WeakMaterials;
		for (UMaterialInterface* Material : Materials)
		{
			WeakMaterials.Add(Material);
		}
		AsyncTask(ENamedThreads::GameThread, [OnComplete, WeakMesh, WeakMaterials]()
		{
			TArray<UMaterialInterface*> LiveMaterials;
			for (const TWeakObjectPtr<UMaterialInterface>& Weak : WeakMaterials)
			{
				LiveMaterials.Add(Weak.Get());
			}
			OnComplete(WeakMesh.Get(), LiveMaterials);
		});
	});
}
