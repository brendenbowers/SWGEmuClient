#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWGEffectsTypes.h"
#include "Simulation/SWGEffectSimulation.h"
#include "SWGEffectsSubsystem.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;
class USkeletalMeshComponent;
class USWGAudioSubsystem;
class USWGParticleEffectComponent;
class USWGTreSubsystem;

/** How SWGGame turns a .msh/.apt appearance into a mesh; registered after the mesh generator exists (SWGEffects cannot see it). */
using FSWGEffectStaticMeshProvider = TFunction<void(const FString& AppearancePath, TFunction<void(UStaticMesh*, const TArray<UMaterialInterface*>&)> OnComplete)>;

/** How SWGGame animates a .sat on a pooled component (mesh plus idle loop); OnReady(true) once it can be drawn. */
using FSWGEffectSkeletalProvider = TFunction<void(const FString& SatPath, USkeletalMeshComponent& Component, TFunction<void(bool)> OnReady)>;

/**
 * Plays .prt particle effects in a world: parse cache, pooled components, distance culling and a global
 * particle budget. One-shot effects free themselves when their last particle dies; loops run until stopped.
 * Tuning cvars: swg.Fx.MaxParticles, swg.Fx.MaxParticlesPerEmitter, swg.Fx.CullDistance, swg.Fx.ForceWorldSpace,
 * swg.Fx.QuadRotationRadiansPerUnit.
 */
UCLASS()
class SWGEFFECTS_API USWGEffectsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Follows Parent (optionally a socket); the effect stops when the parent goes away. PrtPath may be bare ("pt_bolt_red"). */
	FSWGEffectHandle PlayParticleAttached(const FString& PrtPath, USceneComponent* Parent, FName SocketName = NAME_None, float Scale = 1.f, const FLinearColor& Tint = FLinearColor::White, const FTransform& LocalTransform = FTransform::Identity);

	FSWGEffectHandle PlayParticleAtLocation(const FString& PrtPath, const FTransform& WorldTransform, float Scale = 1.f, const FLinearColor& Tint = FLinearColor::White);

	/** Full-control variant used by the two above and by child effects. */
	FSWGEffectHandle PlayParticle(const FString& PrtPath, USceneComponent* AttachParent, FName SocketName, const FTransform& WorldTransform, const FSWGEffectPlayParams& Params);

	void StopAll(bool bImmediate = true);

	FSWGEffectsStats GetStats() const;

	/** Wind in m/s, sim space (x right, y up, z forward); drives each emitter's wind resistance. Stage 7 sets it. */
	void SetWind(const FVector& SimWindMetersPerSecond) { Wind = SimWindMetersPerSecond; }
	const FVector& GetWind() const { return Wind; }
	void SetTerrainHeightProvider(TFunction<bool(const FVector&, float&)> InProvider) { TerrainHeightProvider = MoveTemp(InProvider); }

	/** Parsed .prt by (possibly bare) path; null when missing or a retail zero-byte stub. */
	TSharedPtr<const FSWGParticleEffect> GetOrLoadEffect(const FString& PrtPath);
	static FString NormalizeEffectPath(const FString& PrtPath);

	void SetStaticMeshProvider(FSWGEffectStaticMeshProvider InProvider) { StaticMeshProvider = MoveTemp(InProvider); }
	bool HasStaticMeshProvider() const { return (bool)StaticMeshProvider; }
	void RequestParticleMesh(const FString& MeshPath, TFunction<void(UStaticMesh*, const TArray<UMaterialInterface*>&)> OnComplete);

	void SetSkeletalMeshProvider(FSWGEffectSkeletalProvider InProvider) { SkeletalMeshProvider = MoveTemp(InProvider); }
	bool HasSkeletalMeshProvider() const { return (bool)SkeletalMeshProvider; }

	/** Starts a pooled component animating SatPath; OnReady runs on the game thread unless the component was released meanwhile. */
	void RequestSkeletalParticle(const FString& SatPath, USkeletalMeshComponent& Component, TFunction<void(bool)> OnReady);

	/** Pooled skeletal particle mesh; null at swg.Fx.MaxSkeletalParticles. */
	USkeletalMeshComponent* AcquireSkeletalComponent();
	void ReleaseSkeletalComponent(USkeletalMeshComponent* Component);

	/** Material for a particle shader (.sht): texture + blend mode resolved from the shader and its .eft, shared per shader and grid. */
	UMaterialInterface* GetParticleMaterial(const FString& ShaderPath, int32 GridColumns);

	/**
	 * Unlit materials for geometry that is not a billboard (trail ribbons, saber blades): the shader's texture x a "Tint"
	 * parameter, faded by UV1.x. A shader switch (SWSH) yields one per frame and OutSecondsPerFrame; others yield one.
	 * Shared instances: make a per-user UMaterialInstanceDynamic from them before setting Tint.
	 */
	int32 GetRibbonMaterials(const FString& ShaderPath, TArray<UMaterialInterface*>& OutMaterials, float& OutSecondsPerFrame);

	/** The 100 cm camera-billboarded quad the particle materials expect (UV doubles as the corner code). */
	UStaticMesh* GetBillboardQuadMesh() { return GetQuadMesh(); }

	/** Unit beam strip for appearance beams: X spans -50..50 cm (width), Z 0..100 cm (length); UV0.x runs 1 -> 0 along the length, UV0.y 0 -> 1 across, UV1.x = 1 (no fade). */
	UStaticMesh* GetBeamMesh();

	/** Settings assembled from the cvars (seed filled by the caller). */
	FSWGEffectSimSettings MakeSimSettings() const;

	UInstancedStaticMeshComponent* AcquireInstanceComponent();
	void ReleaseInstanceComponent(UInstancedStaticMeshComponent* Component);
	UStaticMeshComponent* AcquireMeshComponent();
	void ReleaseMeshComponent(UStaticMeshComponent* Component);
	void RecycleEffect(USWGParticleEffectComponent* Effect);

	USWGAudioSubsystem* GetAudioSubsystem() const;
	USWGTreSubsystem* GetTreSubsystem() const;
	int32 GetMaxChildrenPerEffect() const { return 8; }

private:
	friend class FSWGObjectEffectPlacementTest;
	AActor* GetOrCreateHost();
	UStaticMesh* GetQuadMesh();
	UMaterialInterface* LoadParticleMaster(int32 BlendModeIndex);
	UMaterialInterface* BuildRibbonMaterial(const FString& ShaderPath);
	USWGParticleEffectComponent* AcquireEffect();
	bool GetCameraLocation(FVector& OutLocation) const;
	int32 CountLiveParticles() const;

	UPROPERTY()
	TObjectPtr<AActor> Host;
	UPROPERTY()
	TObjectPtr<UStaticMesh> QuadMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> BeamMesh;
	UPROPERTY()
	TArray<TObjectPtr<USWGParticleEffectComponent>> ActiveEffects;
	UPROPERTY()
	TArray<TObjectPtr<USWGParticleEffectComponent>> FreeEffects;
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> FreeInstanceComponents;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> FreeMeshComponents;
	UPROPERTY()
	TMap<FString, TObjectPtr<UMaterialInterface>> ParticleMaterials;
	UPROPERTY()
	TMap<int32, TObjectPtr<UMaterialInterface>> MasterMaterials;
	UPROPERTY()
	TMap<FString, TObjectPtr<UMaterialInterface>> RibbonMaterials;
	UPROPERTY()
	TMap<int32, TObjectPtr<UMaterialInterface>> RibbonMasters;
	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> FreeSkeletalComponents;
	/** Components with a provider request in flight; one released meanwhile waits in Orphaned until it reports. */
	UPROPERTY()
	TSet<TObjectPtr<USkeletalMeshComponent>> PendingSkeletalComponents;
	UPROPERTY()
	TSet<TObjectPtr<USkeletalMeshComponent>> OrphanedSkeletalComponents;

	TMap<FString, TSharedPtr<const FSWGParticleEffect>> EffectCache;
	FSWGEffectStaticMeshProvider StaticMeshProvider;
	FSWGEffectSkeletalProvider SkeletalMeshProvider;
	int32 SkeletalInUse = 0;
	FVector Wind = FVector::ZeroVector;
	TFunction<bool(const FVector&, float&)> TerrainHeightProvider;
	uint32 NextSerial = 1;
	uint32 NextAutoSeed = 1;
	int32 RejectedPlays = 0;
	int32 CulledCount = 0;
	bool bWarnedMissingMasters = false;
};
