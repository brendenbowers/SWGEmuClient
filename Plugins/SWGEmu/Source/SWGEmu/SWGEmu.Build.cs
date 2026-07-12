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
				"CommonUI",
				"CommonInput",
				"GameplayTags",
				"UMG",
				"Landscape",
				"GeometryFramework",
				"GeometryCore",
				"EnhancedInput",
				"ImageCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// For ASWGPlayer's EKeys::MouseX/Y raw-axis mouse-look binding
				// (BindAxisKey) — see SWGPlayer::SetupPlayerInputComponent.
				"InputCore",
			}
		);

		if (Target.bBuildEditor)
		{
			// FSWGSkeletalMeshImporter builds USkeletalMesh/USkeleton assets from
			// our own parsed .skt/.mgn data using the same editor-only APIs the
			// FBX importer uses internally (FSkeletalMeshImportData,
			// SkeletalMeshImportUtils, IMeshBuilderModule) — there's no
			// packaged-build-safe way to construct a real skinned mesh (see
			// world-object-plan.html's "Must work in a packaged/shipping build"
			// box), so this is editor/PIE-only for now, wrapped in WITH_EDITOR.
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"MeshBuilder",
					"MeshUtilitiesCommon",
					"SkeletalMeshUtilitiesCommon",
					"SkeletalMeshDescription",
					// FSWGAnimationImporter's IAnimationDataController-based
					// UAnimSequence construction (same editor-only reasoning).
					"AnimationDataController",
				}
			);
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
