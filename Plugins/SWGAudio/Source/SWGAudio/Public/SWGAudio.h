#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** SWGAudio module: sample decoding and .snd playback. No game-object knowledge. */
class FSWGAudioModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
