#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class USWGSoundInstance;

/** Lightweight reference to a playing sound. Safe to copy and to keep after the sound ends. */
struct SWGAUDIO_API FSWGSoundHandle
{
	TWeakObjectPtr<USWGSoundInstance> Instance;

	/** False for a handle returned when the sound could not be started at all. */
	bool IsValid() const { return Instance.IsValid(); }

	/** True while the sound is scheduled, delayed between loops, or audible. */
	bool IsPlaying() const;

	/** Fades out over FadeOutSeconds (0 = immediately) and ends the sound. */
	void Stop(float FadeOutSeconds = 0.f) const;
};
