#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

class FModelWidgetModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Shaders sit with the code that uses them, under the module rather than at the plugin root.
		const FString ShaderDirectory = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("ModelWidget"))->GetBaseDir(), TEXT("Source"), TEXT("ModelWidget"), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/ModelWidget"), ShaderDirectory);
	}
};

IMPLEMENT_MODULE(FModelWidgetModule, ModelWidget)
