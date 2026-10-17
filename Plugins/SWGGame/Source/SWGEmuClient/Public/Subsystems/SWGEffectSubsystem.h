#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGEffectSubsystem.generated.h"

class USWGAudioSubsystem;
class USWGNetworkSubsystem;
class USWGObjectGraphSubsystem;
class USWGTerrainSubsystem;
struct FSWGClientEffect;
struct FSWGClientEffectCameraShake;
struct FSWGClientEffectLight;
struct FSWGNetMessage;

/**
 * Turns the server's PlayMusic and PlayClientEffect messages into sound and visuals. Every part of
 * a .cef plays through PlayClientEffectAttached / PlayClientEffectAtLocation (FFBK is skipped).
 */
UCLASS()
class SWGEMUCLIENT_API USWGEffectSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Null when the game instance has no effect subsystem (no running game). */
	static USWGEffectSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * Plays a .cef on a component, attached to Socket when it exists (else the component itself). Returns
	 * the sounds started; particles, lights and shake play regardless.
	 */
	int32 PlayClientEffectAttached(const FString& EffectFile, USceneComponent* Parent, FName Socket = NAME_None, const FTransform& LocalTransform = FTransform::Identity);

	/** Plays a .cef at a world (UE) location. Returns the sounds started. */
	int32 PlayClientEffectAtLocation(const FString& EffectFile, const FVector& WorldLocation, const FRotator& Rotation = FRotator::ZeroRotator);

	/**
	 * Plays a .cef's sounds on an object, attached to Hardpoint when the actor has a mesh socket of
	 * that name (else its root). Returns the sounds started; 0 if the object is not spawned.
	 */
	int32 PlayEffectOnObject(const FString& EffectFile, int64 ObjectId, const FString& Hardpoint);
	/** Plays a .cef at an object-relative transform in UE units, following the object. */
	int32 PlayEffectOnObjectTransform(const FString& EffectFile, int64 ObjectId, const FTransform& LocalTransform);

	/**
	 * Plays a .cef at a raw SWG position (x east, y north, z up), building-relative through CellId
	 * when it is non-zero. A Planet other than the current one is ignored.
	 */
	int32 PlayEffectAtLocation(const FString& EffectFile, const FString& Planet, const FVector& RawPosition, int64 CellId);

private:
	friend class FSWGObjectEffectPlacementTest;
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);
	/** Particles, lights and shake of a parsed .cef; Parent null means at Where. */
	void PlayVisuals(const FSWGClientEffect& Effect, USceneComponent* Parent, FName Socket, const FTransform& Where, const FTransform& LocalTransform = FTransform::Identity);
	/** CLGT point light on Parent, or on a host actor at Where when Parent is null; fades out over its duration. */
	void PlayLight(const FSWGClientEffectLight& Light, USceneComponent* Parent, FName Socket, const FTransform& Where, const FTransform& LocalTransform = FTransform::Identity);
	/** CAMS shake for the local camera when it is within range of Origin, scaled down with distance. */
	void PlayCameraShake(const FSWGClientEffectCameraShake& Shake, const FVector& Origin);

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGAudioSubsystem> Audio;

	UPROPERTY()
	TObjectPtr<USWGTerrainSubsystem> Terrain;

	FDelegateHandle MessageHandle;
};
