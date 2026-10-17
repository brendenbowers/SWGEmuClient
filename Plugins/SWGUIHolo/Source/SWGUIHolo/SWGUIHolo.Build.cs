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
			"SWGEffects",
		});

		// The space HUD tests wait for the HUD materials' shaders on the active RHI.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"RHI",
			"RenderCore",
			"SWGAudio",
		});
	}
}
