using UnrealBuildTool;

public class ModelWidget : ModuleRules
{
	public ModelWidget(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Loads at PostConfigInit so its global shaders exist before the global
		// shader map compiles. Engine and UMG are fine that early; game modules
		// are not, so the widget reaches them through an installed provider.
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"RHI",
			"RenderCore",
			"Slate",
			"SlateCore",
			"UMG",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Projects",
		});
	}
}
