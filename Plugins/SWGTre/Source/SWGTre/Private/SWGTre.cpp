#include "SWGTre.h"

#define LOCTEXT_NAMESPACE "FSWGTreModule"

void FSWGTreModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("SWGTre module loaded"));
}

void FSWGTreModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("SWGTre module unloaded"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSWGTreModule, SWGTre)
