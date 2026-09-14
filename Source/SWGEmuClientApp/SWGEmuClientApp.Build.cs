using UnrealBuildTool;

// Primary game module. Intentionally empty: all gameplay lives in the SWGGame
// plugin (module SWGEmuClient) and UI in SWGUI; this just anchors the target.
public class SWGEmuClientApp : ModuleRules
{
	public SWGEmuClientApp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"SWGEmuClient",
		});
	}
}
