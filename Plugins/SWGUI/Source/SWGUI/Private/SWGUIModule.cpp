#include "Modules/ModuleManager.h"
#include "ModelWidget.h"
#include "Subsystems/SWGItemIconSubsystem.h"
#include "Engine/World.h"

class FSWGUIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The model widget plugin loads before the game exists and knows nothing
		// of it; this is where an object id becomes a mesh.
		UModelWidget::SetModelProvider([](UWorld* World, int64 ObjectId, FModelReady OnReady)
		{
			if (USWGItemIconSubsystem* Icons = World->GetSubsystem<USWGItemIconSubsystem>())
			{
				Icons->RequestItemModel(ObjectId, MoveTemp(OnReady));
			}
		});
	}

	virtual void ShutdownModule() override
	{
		UModelWidget::SetModelProvider(nullptr);
	}
};

IMPLEMENT_MODULE(FSWGUIModule, SWGUI)
