using UnrealBuildTool;

public class SWGUI : ModuleRules
{
	public SWGUI(ReadOnlyTargetRules Target) : base(Target)
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
			"SWGEmuClient",
		});
	}
}
