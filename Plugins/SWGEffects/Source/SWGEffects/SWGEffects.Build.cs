// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SWGEffects : ModuleRules
{
	public SWGEffects(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"SWGTre",
				"SWGAnimation",
				"SWGAudio",
				"MeshDescription",
				"StaticMeshDescription",
			}
		);
	}
}
