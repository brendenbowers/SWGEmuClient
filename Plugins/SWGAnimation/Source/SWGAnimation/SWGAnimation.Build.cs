// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SWGAnimation : ModuleRules
{
	public SWGAnimation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"SWGTre",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			}
		);

		if (Target.bBuildEditor)
		{
			// FSWGSkeletalMeshImporter builds USkeletalMesh/USkeleton assets from our own
			// parsed .skt/.mgn data using the same editor-only APIs the FBX importer uses
			// internally — no packaged-build-safe way to construct a real skinned mesh, so
			// this is editor/PIE-only for now, wrapped in WITH_EDITOR.
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"MeshBuilder",
					"MeshUtilitiesCommon",
					"SkeletalMeshUtilitiesCommon",
					"SkeletalMeshDescription",
					// FSWGAnimationImporter's IAnimationDataController-based UAnimSequence
					// construction (same editor-only reasoning).
					"AnimationDataController",
				}
			);
		}
	}
}
