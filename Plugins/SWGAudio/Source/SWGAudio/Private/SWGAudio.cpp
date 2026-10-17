#include "SWGAudio.h"

#include "SWGAudioTypes.h"

DEFINE_LOG_CATEGORY(LogSWGAudio);

void FSWGAudioModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("SWGAudio module loaded"));
}

void FSWGAudioModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("SWGAudio module unloaded"));
}

IMPLEMENT_MODULE(FSWGAudioModule, SWGAudio)
