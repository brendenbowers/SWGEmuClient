#include "SWGEmu.h"

#define LOCTEXT_NAMESPACE "FSWGEmuModule"

void FSWGEmuModule::StartupModule()
{
	// This code will execute after your module is loaded into memory;
	// the exact timing is specified in the .uplugin file per-module
	UE_LOG(LogTemp, Log, TEXT("SWGEmu module loaded"));
}

void FSWGEmuModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.
	// For modules that bind Delegates, it is important to unbind before shutdown.
	// Otherwise, the Slate application may try to invoke callbacks
	// after the module has been unloaded, potentially causing a crash.
	UE_LOG(LogTemp, Log, TEXT("SWGEmu module unloaded"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSWGEmuModule, SWGEmu)
