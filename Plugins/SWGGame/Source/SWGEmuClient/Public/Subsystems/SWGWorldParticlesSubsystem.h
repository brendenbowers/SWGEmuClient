#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWGEffectsTypes.h"
#include "SWGWorldParticlesSubsystem.generated.h"

class USWGEffectsSubsystem;

/** Where an emitter came from; each kind has its own start radius. */
enum class ESWGWorldEmitterKind : uint8
{
	/** A .prt-appearance object: snapshot, interior layout or server spawn. */
	Placed,
	/** An FFAM/RFAM .prt child scattered over the terrain. */
	Flora,
};

struct FSWGWorldParticleStats
{
	int32 PlacedEmitters = 0;
	int32 FloraEmitters = 0;
	int32 PlacedRunning = 0;
	int32 FloraRunning = 0;
	int32 LiveEffects = 0;
	int32 SkeletalParticles = 0;
};

/**
 * Keeps the looping .prt of world objects and terrain flora alive only near the camera. Emitters are plain records
 * (an actor or a flora cell registers them), so a planet of them costs a list, not components. Four times a second the
 * nearest swg.Fx.MaxWorldEmitters within their radius play; others stop beyond radius x swg.Fx.WorldHysteresis.
 */
UCLASS()
class SWGEMUCLIENT_API USWGWorldParticlesSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Returns an id for RemoveEmitter. Owner, when given, mutes the emitter while the actor is hidden (a server object awaiting baselines).
	 *  OwnerRelative places the emitter against the owner (a .cmp particle part) instead of at the owner itself. */
	int32 AddEmitter(ESWGWorldEmitterKind Kind, const FString& PrtPath, const FTransform& WorldTransform, AActor* Owner = nullptr, const FTransform* OwnerRelative = nullptr);
	void RemoveEmitter(int32 EmitterId);

	FSWGWorldParticleStats GetStats() const;

	/** Re-evaluates every emitter now instead of waiting for the next update. */
	void RefreshNow();

private:
	struct FEmitter
	{
		ESWGWorldEmitterKind Kind = ESWGWorldEmitterKind::Placed;
		FString PrtPath;
		FTransform Transform;
		TWeakObjectPtr<AActor> Owner;
		bool bHasOwner = false;
		FTransform OwnerRelative = FTransform::Identity;
		FSWGEffectHandle Handle;
		/** Inside its radius at the last refresh; keeps the hysteresis band sticky. */
		bool bEngaged = false;
	};

	void Refresh();
	void StopEmitter(FEmitter& Emitter);

	TMap<int32, FEmitter> Emitters;
	int32 NextEmitterId = 1;
	float SecondsSinceRefresh = 0.f;
};
