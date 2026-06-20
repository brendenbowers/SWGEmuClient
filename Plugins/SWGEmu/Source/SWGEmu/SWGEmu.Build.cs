// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SWGEmu : ModuleRules
{
	public SWGEmu(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Sockets",
				"Networking",
                "PacketHandler",
                "zlib",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Add private dependencies as needed
			}
		);

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
