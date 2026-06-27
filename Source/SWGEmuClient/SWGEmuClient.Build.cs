// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SWGEmuClient : ModuleRules
{
	public SWGEmuClient(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"CommonUI",
			"CommonInput",
			"SWGEmu"
        });

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"SWGEmuClient",
			"SWGEmuClient/Variant_Platforming",
			"SWGEmuClient/Variant_Platforming/Animation",
			"SWGEmuClient/Variant_Combat",
			"SWGEmuClient/Variant_Combat/AI",
			"SWGEmuClient/Variant_Combat/Animation",
			"SWGEmuClient/Variant_Combat/Gameplay",
			"SWGEmuClient/Variant_Combat/Interfaces",
			"SWGEmuClient/Variant_Combat/UI",
			"SWGEmuClient/Variant_SideScrolling",
			"SWGEmuClient/Variant_SideScrolling/AI",
			"SWGEmuClient/Variant_SideScrolling/Gameplay",
			"SWGEmuClient/Variant_SideScrolling/Interfaces",
			"SWGEmuClient/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
