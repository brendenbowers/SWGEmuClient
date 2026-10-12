#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "TRE/SWGParticleEffectReader.h"

/**
 * Pure .prt particle simulation (no UObjects). Everything runs in "sim space": metres in the SWG native
 * axes (x right, y up, z forward), which is where the .prt data lives. The renderer converts to Unreal.
 * Field meanings follow the retail client (Docs/RetailParticleFormat.md); what the client does that is not
 * modelled here: scene ambient colour blending, PTCL rotation curves, the mesh axis rules.
 */

/** Budgets and tunables. Defaults follow the retail client where it was traced. */
struct SWGEFFECTS_API FSWGEffectSimSettings
{
	/** World sim space (x right, y up, z forward), metres; false when no terrain exists at the position. */
	TFunction<bool(const FVector&, float&)> TerrainHeight;
	uint32 Seed = 1;
	int32 MaxParticlesPerEmitter = 1024;
	/** Debug override: True makes every emitter spawn in the world (retail: Params.bLocalSpace clear). */
	bool bForceWorldSpace = false;
	/** PTQD rotation curve unit: one turn (retail multiplies by 2*pi, 0x1923c84). */
	float QuadRotationRadiansPerUnit = UE_TWO_PI;
	/** A run is never shorter than this, whatever the params say. */
	float MinRunWindowSeconds = 0.05f;
	/** Generate-by-distance: Rate is metres between particles (0.1-1.5 in the data), floored here. */
	float MinDistanceSpacing = 0.01f;
	/** Flocking: random heading change per second; the retail turn rule (Params.FlockTurnAngle) is only approximated. */
	float FlockWanderRadiansPerSecond = 2.5f;
	float FlockTurnRateScale = 0.5f;
	/** Flocking: fraction of the cage half-extent where the inward push starts. */
	float FlockCageSoftFraction = 0.8f;
	/** Flocking: neighbours considered per particle, to bound the O(n^2) pass. */
	int32 FlockMaxNeighbours = 24;
};

/** Where the effect currently is, in sim space. Particles that live in the world keep their own positions. */
struct FSWGEffectFrame
{
	FVector Location = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	double Scale = 1.0;
	/** Wind velocity in m/s, sim space; scaled per emitter by (1 - Params.GetWindResistance()) and only moves world-space particles. Stage 7 supplies it. */
	FVector Wind = FVector::ZeroVector;
};

/** A PATT child effect a particle asks for. */
struct FSWGChildSpawnRequest
{
	FString EffectPath;
	int32 EmitterIndex = 0;
	uint32 ParticleId = 0;
	FVector WorldPosition = FVector::ZeroVector;
	FQuat WorldRotation = FQuat::Identity;
	/** Mode 2 (inferred "trail"): the child follows the particle until it dies. */
	bool bFollowParticle = false;
};

/** What the renderer needs for one particle. */
struct FSWGParticleRenderData
{
	uint32 Id = 0;
	FVector WorldPosition = FVector::ZeroVector;
	FVector WorldVelocity = FVector::ZeroVector;
	FLinearColor Color = FLinearColor::White;
	int32 Frame = 0;
	/** Quad size in metres as authored, already multiplied by the effect scale. Retail treats them as half-extents (centre +- Length * axis1 +- Width * axis2); check the quad material does too. Mesh particles: Width = uniform scale. */
	float Width = 0.f;
	float Length = 0.f;
	float RotationRadians = 0.f;
	/** Unit axis the quad's length follows (Params.Orientation 1 and 2); zero = face the camera. */
	FVector AlignAxis = FVector::ZeroVector;
	/** Mesh particles: orientation from the PTMH rotation curves (turns) in sim space. */
	FQuat MeshRotation = FQuat::Identity;
	float LifeFraction = 0.f;
};

struct FSWGSimParticle
{
	uint32 Id = 0;
	bool bTerrainHit = false;
	/** Sim space for world-space emitters, effect-local metres otherwise. */
	FVector Position = FVector::ZeroVector;
	FVector Direction = FVector(0.0, 1.0, 0.0);
	/** Gravity and steering velocity on top of Direction x BaseSpeed. Flocks keep their whole velocity here. */
	FVector ExtraVelocity = FVector::ZeroVector;
	float BaseSpeed = 0.f;
	float Gravity = 0.f;
	float Age = 0.f;
	float Lifetime = 1.f;
	float RandomRotation = 0.5f;
	float RandomLength = 0.5f;
	float RandomWidth = 0.5f;
	float RandomAlpha = 0.5f;
	/** Ramp position for colour ramps with Mode1 = 1. */
	float RandomColor = 0.5f;
	/** Signed turns added to the rotation curve (0x7a4350): 1 or random 0..1, negated when the spin direction is random and flipped. */
	float InitialRotation = 1.f;
	/** Life fraction at which the PATT child is requested; negative = none or already requested. */
	float ChildTrigger = -1.f;
};

class SWGEFFECTS_API FSWGEmitterSimulation
{
public:
	/** Timing levels are the enclosing effect/group PTIMs: all add start delay; the emitter's own, else the group's, drives the loops (the effect level's loop count is ignored: one-shot hit effects carry -1 there). */
	void Initialize(const FSWGParticleEmitter& InEmitter, const FSWGEffectTiming& EffectTiming, const FSWGEffectTiming& GroupTiming,
		const FSWGEffectSimSettings& InSettings, uint32 EmitterSeed, int32 InEmitterIndex);

	void Update(float DeltaSeconds, const FSWGEffectFrame& Frame, TArray<FSWGChildSpawnRequest>& OutChildren);

	/** Stops emitting; live particles finish their lives unless bKillParticles. */
	void Stop(bool bKillParticles);

	/** No more spawning will happen (all runs done or stopped). */
	bool IsEmissionFinished() const { return Phase == EPhase::Finished; }
	bool IsFinished() const { return Phase == EPhase::Finished && Particles.IsEmpty(); }

	const TArray<FSWGSimParticle>& GetParticles() const { return Particles; }
	int32 GetParticleCount() const { return Particles.Num(); }
	const FSWGParticleEmitter& GetEmitterData() const { return *Emitter; }
	bool IsWorldSpace() const { return bWorldSpace; }
	bool IsAligned() const { return bAligned; }
	bool IsFlocking() const { return Emitter->Params.IsFlocking(); }
	float GetStartDelay() const { return StartDelaySeconds; }
	float GetRunWindow() const { return RunWindowSeconds; }
	int32 GetRunsStarted() const { return RunsStarted; }
	int32 GetParticleCap() const { return ParticleCap; }

	/** Flocks stay inside this box (effect-local sim space, metres). */
	FVector GetCageCenter() const { return CageCenter; }
	FVector GetCageHalfExtent() const { return CageHalfExtent; }

	/** Where a particle is in sim space right now. */
	FVector ToWorldPosition(const FSWGSimParticle& Particle, const FSWGEffectFrame& Frame) const;

	void GatherRenderData(const FSWGEffectFrame& Frame, TArray<FSWGParticleRenderData>& OutData) const;
	bool TryGetParticleWorldPosition(uint32 ParticleId, const FSWGEffectFrame& Frame, FVector& OutPosition) const;

	/** Flipbook frame for a particle: fps -1 plays the strip once across its life, fps > 0 loops, anything else holds the first frame (0x7b7100). */
	static int32 ComputeFlipbookFrame(const FSWGParticleTexture& Texture, float AgeSeconds, float LifeFraction);

private:
	/** Drain: the run is over and the next one waits for the live particles to die (unless Params.bIgnoreLiveParticles). */
	enum class EPhase : uint8 { Delay, Running, Drain, Finished };

	void BeginRun(const FSWGEffectFrame& Frame, float TimeIntoStep, float StepLength);
	/** True when another run follows. */
	bool EndRun();
	/** Spawns the run's burst batch, unless it would exceed the particle cap. */
	void FireBurst(float TimeIntoStep, float StepLength, const FSWGEffectFrame& Frame);
	void EmitDuring(float StepSeconds, float StepOffset, float StepLength, const FSWGEffectFrame& Frame);
	void SpawnParticle(float CurveTime, float TimeIntoStep, float StepLength, const FSWGEffectFrame& Frame);
	void AdvanceParticle(FSWGSimParticle& Particle, float DeltaSeconds, const FSWGEffectFrame& Frame);
	void StepFlock(float DeltaSeconds);
	FVector ComputeVelocity(const FSWGSimParticle& Particle, const FSWGEffectFrame& Frame) const;
	void CheckChild(FSWGSimParticle& Particle, const FSWGEffectFrame& Frame, TArray<FSWGChildSpawnRequest>& OutChildren) const;
	float SampleCurve(ESWGEmitterCurve Curve, float CurveTime);
	float SampleRange(float MinValue, float MaxValue);
	FVector RandomUnitVector();
	/** A point on the emission shape's outline, nominally within [-1, 1] on each axis (ParticleEmitterShape*::getPosition). */
	FVector SampleShapeVector();
	float ComputeRunWindow();

	const FSWGParticleEmitter* Emitter = nullptr;
	FSWGEffectSimSettings Settings;
	FRandomStream Random;
	int32 EmitterIndex = 0;

	EPhase Phase = EPhase::Delay;
	bool bStopRequested = false;
	bool bWorldSpace = false;
	bool bAligned = false;
	/** The first emission of a run is guaranteed (Params.bEmitOnStart). */
	bool bStartParticlePending = false;
	int32 RunsStarted = 0;
	int32 RunsAllowed = 1;
	float StartDelaySeconds = 0.f;
	float DelayRemaining = 0.f;
	float RunTime = 0.f;
	float RunWindowSeconds = 1.f;
	/** Gap before every run after the first. */
	float LoopDelayMin = 0.f;
	float LoopDelayMax = 0.f;
	float SpawnAccumulator = 0.f;
	float DistanceAccumulator = 0.f;
	float HorizontalBounce = 0.5f;
	float VerticalBounce = 0.5f;
	int32 ParticleCap = 0;
	uint32 NextParticleId = 1;
	bool bHasPreviousFrame = false;
	FSWGEffectFrame PreviousFrame;
	FVector CageCenter = FVector::ZeroVector;
	FVector CageHalfExtent = FVector::ZeroVector;
	TArray<FSWGSimParticle> Particles;
};

/** One playing .prt: owns an emitter simulation per EMTR. */
class SWGEFFECTS_API FSWGEffectSimulation
{
public:
	void Initialize(TSharedRef<const FSWGParticleEffect> InEffect, const FSWGEffectSimSettings& InSettings);

	void Update(float DeltaSeconds, const FSWGEffectFrame& Frame);

	/** bImmediate kills every particle; otherwise emission stops and particles fade out naturally. */
	void Stop(bool bImmediate);

	bool IsStopped() const { return bStopped; }
	/** Emission over and every particle dead: one-shots reach this on their own, loops only after Stop. */
	bool IsFinished() const;

	int32 GetEmitterCount() const { return Emitters.Num(); }
	const FSWGEmitterSimulation& GetEmitter(int32 EmitterIndex) const { return Emitters[EmitterIndex]; }
	int32 GetLiveParticleCount() const;
	float GetElapsedSeconds() const { return ElapsedSeconds; }
	const FSWGParticleEffect& GetEffect() const { return *Effect; }

	/** Child effect requests produced since the last call. */
	TArray<FSWGChildSpawnRequest> ConsumeChildRequests();

private:
	TSharedPtr<const FSWGParticleEffect> Effect;
	TArray<FSWGEmitterSimulation> Emitters;
	TArray<FSWGChildSpawnRequest> PendingChildren;
	float ElapsedSeconds = 0.f;
	bool bStopped = false;
};
