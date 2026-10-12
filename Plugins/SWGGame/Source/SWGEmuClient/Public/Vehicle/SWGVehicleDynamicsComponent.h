#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Playback/SWGSoundHandle.h"
#include "TRE/SWGClientDataFileReader.h"
#include "SWGEffectsTypes.h"
#include "Vehicle/SWGVehicleEngine.h"
#include "Vehicle/SWGVehicleHoverDynamics.h"
#include "SWGVehicleDynamicsComponent.generated.h"

class USWGAudioSubsystem;

/**
 * Hover tilt and engine sound for a vehicle mount (retail VehicleHoverDynamicsClient + the VTHR/VSND data
 * of clientdata/vehicle/*.iff). Tilts the vehicle's body mesh and its riders; the actor itself is untouched.
 * Created by USWGVehicleSubsystem once the body has attached.
 */
UCLASS()
class SWGEMUCLIENT_API USWGVehicleDynamicsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGVehicleDynamicsComponent();

	void Setup(const FString& InClientDataPath, const FSWGClientDataFile& InClientData);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	const FString& GetClientDataPath() const { return ClientDataPath; }
	const FSWGVehicleEngineSounds& GetEngineSounds() const { return EngineSounds; }
	const FSWGHoverParams& GetHoverParams() const { return Params; }
	const FSWGHoverOutput& GetLastOutput() const { return LastOutput; }
	ESWGEngineState GetEngineState() const { return EngineState; }
	int32 GetThrustLevelCount() const { return ThrustLevels; }

	/** Stops the engine loops (the vehicle is going away or silenced). */
	void StopEngine(float FadeSeconds);

private:
	USWGAudioSubsystem* GetAudio() const;
	UPrimitiveComponent* FindBody();
	void SampleTerrain(float& OutPitch, float& OutRoll, float& OutHeight, bool& bOutHasGround) const;
	void ApplyTilt(const FRotator& Tilt, float HeightOffset);
	void UpdateEngine(float SpeedFraction);
	void UpdateEffects(float SpeedFraction);
	void StopEffects();
	USceneComponent* MakeEffectAnchor(const FString& Hardpoint);
	void StartLoop(FSWGSoundHandle& Handle, const FString& SoundPath, float FadeInSeconds);

	FString ClientDataPath;
	FSWGVehicleEngineSounds EngineSounds;
	int32 ThrustLevels = 0;
	TArray<FSWGClientDataThrust> Thrusts;
	TArray<FSWGClientDataGroundEffect> GroundEffects;

	/** The thrust level whose thruster particles play (-1 none) and the ground trail kind (0 land, 1 water, -1 none). */
	int32 ActiveThrustIndex = -1;
	int32 ActiveGroundKind = -1;
	TArray<FSWGEffectHandle> EffectHandles;
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> EffectAnchors;

	FSWGHoverParams Params;
	FSWGHoverState State;
	FSWGHoverOutput LastOutput;

	ESWGEngineState EngineState = ESWGEngineState::Idle;
	bool bEngineStarted = false;
	FSWGSoundHandle IdleHandle;
	FSWGSoundHandle RunHandle;

	TWeakObjectPtr<UPrimitiveComponent> Body;
	FTransform BodyBase;
	bool bBodyBaseCaptured = false;
};
