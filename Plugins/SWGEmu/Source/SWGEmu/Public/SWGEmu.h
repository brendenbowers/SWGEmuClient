#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * SWGEmu module — SOE UDP protocol implementation for Star Wars Galaxies EMU.
 *
 * This plugin provides:
 * - Phase 1: SOE Protocol Layer (encryption, compression, reliability)
 * - Phase 2: Message Dispatch System
 * - Phase 3: Login & Zone Server Handoff (UMG UI helpers)
 * - Phase 4: Object Manager & Actor Integration
 * - Phase 5: Movement System
 *
 * Primary entry point: USWGNetworkSubsystem
 */
class FSWGEmuModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
