// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class SWGAudio : ModuleRules
{
	public SWGAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AudioExtensions", // IAudioProxyDataFactory, needed by USoundWaveProcedural subclasses
				"SWGTre",
			}
		);

		// minimp3 is header-only; its implementation is compiled once in SWGMp3Decoder.cpp.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ThirdParty", "minimp3"));
	}
}
