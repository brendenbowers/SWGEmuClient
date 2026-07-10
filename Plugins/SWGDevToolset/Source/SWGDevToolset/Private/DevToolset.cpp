// Copyright Epic Games, Inc. All Rights Reserved.

#include "DevToolset.h"
#include "Engine/Engine.h"
#include "Misc/StringOutputDevice.h"

#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif

FString UDevToolset::ExecuteConsoleCommand(const FString& Command)
{
	if (!GEngine)
	{
		return FString();
	}

	FStringOutputDevice OutputDevice;
	OutputDevice.SetAutoEmitLineTerminator(true);

	// Prefer an active PIE world so gameplay-only commands work; fall back to any
	// editor world context (there's no active PIE session outside of Play).
	UWorld* World = GEngine->GetCurrentPlayWorld();
	if (!World)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				World = Context.World();
				break;
			}
		}
	}

	GEngine->Exec(World, *Command, OutputDevice);

	return OutputDevice;
}

bool UDevToolset::RecompileLiveCoding()
{
#if PLATFORM_WINDOWS
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>("LiveCoding");
	if (!LiveCoding || !LiveCoding->IsEnabledForSession())
	{
		return false;
	}

	ELiveCodingCompileResult Result = ELiveCodingCompileResult::Failure;
	LiveCoding->Compile(ELiveCodingCompileFlags::WaitForCompletion, &Result);

	return Result == ELiveCodingCompileResult::Success || Result == ELiveCodingCompileResult::NoChanges;
#else
	return false;
#endif
}
