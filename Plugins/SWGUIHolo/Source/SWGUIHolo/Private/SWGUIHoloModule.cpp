#include "Modules/ModuleManager.h"
#include "InputCoreTypes.h"
#include "SWGPlacementViewMode.h"
#include "SWGPlacementDatapadMode.h"
#include "SWGPlacementMapMode.h"

class FSWGUIHoloModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The placement widget (SWGUIWindow) builds its keys from this registry and never names a holo class.
		FSWGPlacementViewRegistry::Register({ TEXT("Datapad"), NSLOCTEXT("SWGUIHolo", "PlacementDatapad", "Datapad"), EKeys::H, USWGPlacementDatapadMode::StaticClass() });
		FSWGPlacementViewRegistry::Register({ TEXT("Map"), NSLOCTEXT("SWGUIHolo", "PlacementMap", "Holo map"), EKeys::M, USWGPlacementMapMode::StaticClass() });
	}

	virtual void ShutdownModule() override
	{
		FSWGPlacementViewRegistry::Unregister(TEXT("Datapad"));
		FSWGPlacementViewRegistry::Unregister(TEXT("Map"));
	}
};

IMPLEMENT_MODULE(FSWGUIHoloModule, SWGUIHolo)
