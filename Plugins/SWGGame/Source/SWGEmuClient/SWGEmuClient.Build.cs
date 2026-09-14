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
			"GameplayTags",
			"Landscape",
			"GeometryFramework",
			"GeometryCore",
			"GeometryScriptingCore",
			"SWGEmu",
			"SWGTre",
			"SWGAnimation"
        });

		PrivateDependencyModuleNames.AddRange(new string[] { });

		if (Target.bBuildEditor)
		{
			// USWGMeshGeneratorSubsystem::GetOrBuildLocomotionBlendSpace registers
			// its generated UBlendSpace assets via FAssetRegistryModule (WITH_EDITOR
			// only) — same reasoning as SWGAnimation's own editor-only asset building.
			// MeshUtilities is needed directly for IMeshUtilities (used to call
			// FSWGSkeletalMeshImporter::BuildSkeletalMeshData's worker-thread build
			// off the game thread — see USWGMeshGeneratorSubsystem::Initialize).
			PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry", "MeshUtilities" });
		}

		PublicIncludePaths.AddRange(new string[] {
			ModuleDirectory,
			System.IO.Path.Combine(ModuleDirectory, "Variant_Platforming"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Platforming/Animation"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Combat"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Combat/AI"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Combat/Animation"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Combat/Gameplay"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Combat/Interfaces"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Combat/UI"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_SideScrolling"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_SideScrolling/AI"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_SideScrolling/Gameplay"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_SideScrolling/Interfaces"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_SideScrolling/UI")
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
