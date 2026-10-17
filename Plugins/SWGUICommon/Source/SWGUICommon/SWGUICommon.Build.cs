using UnrealBuildTool;

public class SWGUICommon : ModuleRules
{
	public SWGUICommon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"UMG",
			"Slate",
			"SlateCore",
			"CommonUI",
			"CommonInput",
			"GameplayTags",
			"DeveloperSettings",
			"SWGEmu",
			"SWGTre",
			"SWGEmuClient",
			"ModelWidget",
			"GeometryCore",
			"GeometryFramework",
			"AnimationCore",
		});

		// Playback only; no SWGAudio type appears in this module's headers.
		PrivateDependencyModuleNames.Add("SWGAudio");
	}
}
