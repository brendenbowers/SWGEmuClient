#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * SWGTre module — read-only parsing of .tre archives, IFF chunks, and terrain data.
 *
 * Primary entry point: USWGTreSubsystem
 */
class FSWGTreModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
