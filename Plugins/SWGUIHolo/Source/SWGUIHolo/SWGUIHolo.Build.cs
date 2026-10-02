using UnrealBuildTool;

public class SWGUIHolo : ModuleRules
{
	public SWGUIHolo(ReadOnlyTargetRules Target) : base(Target)
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
			"SWGUICommon",
			"SWGUICore",
		});
	}
}
