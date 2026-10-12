#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSWGFx, Log, All);

/** SWGEffects module: .prt particle simulation and rendering. No game-object knowledge. */
class FSWGEffectsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
