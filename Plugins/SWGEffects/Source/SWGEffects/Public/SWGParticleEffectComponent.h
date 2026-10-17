#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Playback/SWGSoundHandle.h"
#include "SWGEffectsTypes.h"
#include "Simulation/SWGEffectSimulation.h"
#include "SWGParticleEffectComponent.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USWGEffectsSubsystem;

/** One .sat particle: its pooled component, shown once the mesh and idle loop are in. */
USTRUCT()
struct FSWGSkeletalParticleRender
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;
	bool bReady = false;
	bool bFailed = false;
};

/** PTMH emitter: the mesh arrives asynchronously, particles then borrow pooled mesh components. */
USTRUCT()
struct FSWGMeshEmitterRender
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> Materials;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Active;
	bool bRequested = false;
	bool bLoggedUnsupported = false;

	/** .sat emitters (ambient animals) draw skeletal components instead. */
	bool bSkeletal = false;
	FString SatPath;
	UPROPERTY()
	TArray<FSWGSkeletalParticleRender> Skeletal;
};

/**
 * One playing .prt. The simulation runs in sim space; each quad emitter draws through its own pooled
 * UInstancedStaticMeshComponent (M_SWGParticle_* billboards the unit quad in WPO from per-instance custom data).
 * Particles that outlive the emitter ("trailing" emitters) stay in the world; others ride with this component.
 * Driven by USWGEffectsSubsystem::Tick, not by its own tick.
 */
UCLASS(ClassGroup = (SWG))
class SWGEFFECTS_API USWGParticleEffectComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** Per-instance custom data layout shared with the material builder. */
	static constexpr int32 CustomDataFloats = 11;
	/** PATT children may nest this deep. */
	static constexpr int32 MaxChildDepth = 3;
	enum ECustomData { ColorR, ColorG, ColorB, ColorA, Frame, WidthCm, LengthCm, RotationRadians, AxisX, AxisY, AxisZ };

	void BeginEffect(USWGEffectsSubsystem& InSubsystem, const FString& InPath, TSharedRef<const FSWGParticleEffect> Effect, const FSWGEffectPlayParams& Params, uint32 InSerial);

	/** Advances and draws. bCulled hides the effect and freezes it. */
	void TickEffect(float DeltaSeconds, bool bCulled);

	/** Stops emission; bImmediate also kills particles, stops sounds and hides the effect. */
	void StopEffect(bool bImmediate);

	/** Gives pooled components back; the component itself returns to the pool. */
	void ReleaseResources();

	/** Emission over and nothing left to draw. */
	bool IsEffectFinished() const { return !bActive || Simulation.IsFinished(); }
	bool IsEffectActive() const { return bActive && !Simulation.IsFinished(); }
	bool IsCulled() const { return bCulled; }
	float GetSecondsCulled() const { return SecondsCulled; }
	uint32 GetSerial() const { return Serial; }
	const FString& GetEffectPath() const { return EffectPath; }
	int32 GetChildDepth() const { return PlayParams.ChildDepth; }
	int32 GetLiveParticleCount() const { return bActive ? Simulation.GetLiveParticleCount() : 0; }
	const FSWGEffectSimulation& GetSimulation() const { return Simulation; }

	/** Parent whose destruction should stop this effect (attached plays). */
	TWeakObjectPtr<USceneComponent> AttachParentWatch;
	bool bHasAttachParent = false;

private:
	FSWGEffectFrame MakeFrame() const;
	void UpdateQuadEmitter(int32 EmitterIndex, const TArray<FSWGParticleRenderData>& Data);
	void UpdateMeshEmitter(int32 EmitterIndex, const TArray<FSWGParticleRenderData>& Data);
	static FTransform MakeMeshParticleTransform(const FSWGParticleRenderData& Particle);
	void UpdateSkeletalEmitter(int32 EmitterIndex, const TArray<FSWGParticleRenderData>& Data);
	void OnSkeletalReady(int32 EmitterIndex, USkeletalMeshComponent* Mesh, bool bPlaying);
	void HandleChildren(const FSWGEffectFrame& Frame);
	void UpdateSounds();
	void SetEmittersVisible(bool bMakeVisible);
	void OnMeshReady(int32 EmitterIndex, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials);

	struct FPendingSound
	{
		float DelaySeconds = 0.f;
		FString SoundPath;
	};

	struct FChildLink
	{
		FSWGEffectHandle Handle;
		int32 EmitterIndex = 0;
		uint32 ParticleId = 0;
		bool bFollow = false;
	};

	UPROPERTY()
	TObjectPtr<USWGEffectsSubsystem> Subsystem;
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> QuadInstances;
	UPROPERTY()
	TArray<FSWGMeshEmitterRender> MeshEmitters;

	FSWGEffectSimulation Simulation;
	FSWGEffectPlayParams PlayParams;
	FString EffectPath;
	TArray<FPendingSound> PendingSounds;
	TArray<FSWGSoundHandle> PlayingSounds;
	TArray<FChildLink> Children;
	TArray<FSWGParticleRenderData> ScratchRenderData;
	TArray<FTransform> ScratchTransforms;
	float SecondsCulled = 0.f;
	uint32 Serial = 0;
	bool bActive = false;
	bool bCulled = false;
	bool bHidden = false;
};
