#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SWGAudioTypes.h"
#include "SWGAudioMix.generated.h"

class FAudioDevice;
class USoundAttenuation;
class USoundClass;
class USoundConcurrency;
class USoundMix;

/**
 * Sound classes, mix, attenuation and concurrency built in code.
 * Master > one class per bus > one class per retail category (plus a Voice class under Creature).
 * Bus volumes are SoundMix class adjusters applied to the bus and its children.
 */
UCLASS()
class SWGAUDIO_API USWGAudioMix : public UObject
{
	GENERATED_BODY()

public:
	void Build();

	USoundClass* GetBusClass(ESWGAudioBus Bus) const;
	/** Class for a retail category value; unknown values fall back to the Effects bus class. */
	USoundClass* GetCategoryClass(int32 CategoryValue, bool bVoice = false) const;
	USoundConcurrency* GetConcurrency(int32 CategoryValue, bool bVoice = false) const;

	/** Retail inverse-distance falloff: full volume inside DistanceMeters, 1/d out to DistanceMeters * AttenuationFalloffFactor, silent beyond. */
	USoundAttenuation* GetAttenuation(float DistanceMeters);

	/** Silence point as a multiple of the template's full-volume distance. */
	static constexpr float AttenuationFalloffFactor = 4.f;
	/** Used for a forced-spatial 2D template that has no distance. */
	static constexpr float DefaultDistanceMeters = 10.f;
	/** Retail minimum distance for a 3D template whose distance is 0 or less (SWGEmu.exe 0x40f590). */
	static constexpr float Retail3dDefaultDistanceMeters = 2.f;

	void SetBusVolume(ESWGAudioBus Bus, float Volume);
	float GetBusVolume(ESWGAudioBus Bus) const { return BusVolumes[static_cast<int32>(Bus)]; }

	/** Pushes the mix on Device (or re-applies volumes if already pushed there). Safe to call every play. */
	void ApplyToDevice(FAudioDevice* Device);

private:
	USoundClass* MakeClass(const TCHAR* Name, USoundClass* Parent);

	UPROPERTY()
	TArray<TObjectPtr<USoundClass>> BusClasses;

	UPROPERTY()
	TMap<int32, TObjectPtr<USoundClass>> CategoryClasses;

	UPROPERTY()
	TObjectPtr<USoundClass> VoiceClass;

	UPROPERTY()
	TMap<int32, TObjectPtr<USoundConcurrency>> CategoryConcurrency;

	UPROPERTY()
	TObjectPtr<USoundConcurrency> VoiceConcurrency;

	UPROPERTY()
	TMap<int32, TObjectPtr<USoundAttenuation>> AttenuationByMeters;

	UPROPERTY()
	TObjectPtr<USoundMix> Mix;

	TArray<float> BusVolumes;
	uint32 AppliedDeviceId = INDEX_NONE;
};
