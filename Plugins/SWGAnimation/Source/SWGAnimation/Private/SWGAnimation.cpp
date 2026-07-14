#include "SWGAnimation.h"

#define LOCTEXT_NAMESPACE "FSWGAnimationModule"

void FSWGAnimationModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("SWGAnimation module loaded"));
}

void FSWGAnimationModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("SWGAnimation module unloaded"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSWGAnimationModule, SWGAnimation)
