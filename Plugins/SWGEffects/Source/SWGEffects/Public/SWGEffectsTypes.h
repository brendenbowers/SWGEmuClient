#pragma once

#include "CoreMinimal.h"
#include "Common/SWGWorldScale.h"
#include "UObject/WeakObjectPtrTemplates.h"

class USWGParticleEffectComponent;

/** Sim space (metres, SWG native axes) <-> Unreal world (cm, UE axes). Same cyclic axis relabeling SWGToUnrealSpace uses. */
namespace SWGFx
{
	FORCEINLINE FVector UnrealToSim(const FVector& UnrealPosition) { return FVector(UnrealPosition.Y, UnrealPosition.Z, UnrealPosition.X) / SWGWorldScale; }
	FORCEINLINE FVector SimToUnreal(const FVector& SimPosition) { return FVector(SimPosition.Z, SimPosition.X, SimPosition.Y) * SWGWorldScale; }
	/** Directions and velocities: the same relabeling without the metre/cm factor. */
	FORCEINLINE FVector UnrealToSimDirection(const FVector& Direction) { return FVector(Direction.Y, Direction.Z, Direction.X); }
	FORCEINLINE FVector SimToUnrealDirection(const FVector& Direction) { return FVector(Direction.Z, Direction.X, Direction.Y); }
	FORCEINLINE FQuat UnrealToSimRotation(const FQuat& Rotation) { return SWGUnrealToNativeRotation(Rotation); }
	FORCEINLINE FQuat SimToUnrealRotation(const FQuat& Rotation) { return SWGNativeToUnrealRotation(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W); }
}

struct FSWGEffectPlayParams
{
	float Scale = 1.f;
	/** RGB multiplies the particle colour, A multiplies its alpha. */
	FLinearColor Tint = FLinearColor::White;
	/** 0 = pick one per play. A fixed seed replays identically. */
	uint32 Seed = 0;
	/** Nesting level of PATT children, to stop runaway chains. */
	int32 ChildDepth = 0;
	bool bPlaySounds = true;
};

/** Safe reference to a playing effect; pooled components are recycled, so the handle goes stale rather than dangling. */
struct SWGEFFECTS_API FSWGEffectHandle
{
	TWeakObjectPtr<USWGParticleEffectComponent> Effect;
	/** Bumped on every reuse of a pooled component so an old handle cannot stop a new effect. */
	uint32 Serial = 0;

	bool IsValid() const;
	/** Emitting or still has live particles. */
	bool IsActive() const;
	/** Stops emission; bImmediate also removes the live particles. */
	void Stop(bool bImmediate = false) const;
};

struct FSWGEffectsStats
{
	int32 ActiveEffects = 0;
	int32 CulledEffects = 0;
	int32 LiveParticles = 0;
	int32 PooledEffects = 0;
	int32 PooledInstanceComponents = 0;
	int32 PooledMeshComponents = 0;
	int32 SkeletalParticles = 0;
	int32 CachedEffects = 0;
	int32 CachedMaterials = 0;
	int32 RejectedPlays = 0;
};
