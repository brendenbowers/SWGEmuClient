#pragma once

#include "CoreMinimal.h"
#include "Objects/World/SWGStaticProp.h"
#include "Playback/SWGSoundHandle.h"
#include "SWGSoundEmitter.generated.h"

class USWGAudioSubsystem;

/**
 * A sound object (object/soundobject/*.iff): no mesh, just the .cdf ASND loops at its transform.
 * Spawned from the world snapshot, interior layouts and the server like any other static object.
 * A loop only plays while the listener is inside its audible radius, driven by USWGAmbienceSubsystem,
 * so a planet full of emitters never keeps more than a handful of decoded loops alive.
 */
UCLASS()
class SWGEMUCLIENT_API ASWGSoundEmitter : public ASWGStaticProp
{
	GENERATED_BODY()

public:
	ASWGSoundEmitter();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** True for the sound object templates, which never get a mesh. */
	static bool IsSoundObjectTemplate(const FString& TemplatePath);

	/** Reads the template's loops and registers with the ambience subsystem. Game thread. */
	void InitFromTemplate(const FString& InTemplatePath);

	/** Starts and stops the loops for a listener position; bAllowed false (over the loop budget) counts as out of range. */
	void UpdateLoops(const FVector& ListenerLocation, bool bAllowed);

	/** Largest audible radius over all loops, cm; 0 until resolved. */
	float GetMaxRadius() const { return MaxRadius; }

	const FString& GetEmitterTemplatePath() const { return TemplatePath; }
	int32 GetActiveLoopCount() const;
	TArray<FString> GetLoopDescriptions() const;

private:
	struct FLoop
	{
		FString Path;
		float Radius = 0.f;
		FSWGSoundHandle Handle;
		/** Started and not yet stopped for range; a loop that ends on its own is not restarted. */
		bool bActive = false;
	};

	void StopLoops();
	USWGAudioSubsystem* GetAudio() const;

	FString TemplatePath;
	TArray<FLoop> Loops;
	int32 ParticleEmitterId = 0;
	float MaxRadius = 0.f;
};
