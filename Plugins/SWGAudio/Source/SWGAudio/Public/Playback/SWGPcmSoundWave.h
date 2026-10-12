#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "SWGPcmSoundWave.generated.h"

class FSWGPcmStreamSource;

/** Procedural wave that pulls its audio from a shared-PCM stream source on the audio thread. */
UCLASS()
class SWGAUDIO_API USWGPcmSoundWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	USWGPcmSoundWave(const FObjectInitializer& ObjectInitializer);

	/** Configures channels/rate from the source and starts feeding from it. */
	void Setup(TSharedPtr<FSWGPcmStreamSource> InSource);

	TSharedPtr<FSWGPcmStreamSource> GetSource() const { return Source; }

	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;

private:
	TSharedPtr<FSWGPcmStreamSource> Source;
};
