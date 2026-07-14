#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * SWGAnimation module — mesh/skeleton/animation parsing, runtime playback, and
 * editor-only asset importers for Star Wars Galaxies model data.
 */
class FSWGAnimationModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
