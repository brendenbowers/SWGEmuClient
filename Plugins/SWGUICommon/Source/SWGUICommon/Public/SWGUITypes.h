#pragma once

#include "CoreMinimal.h"
#include "SWGUITypes.generated.h"

/** How a feature is shown. Each UI plugin (SWGUICore, SWGUIWindow, SWGUIHolo) provides one; each can be absent. */
UENUM(BlueprintType)
enum class ESWGUIPresentation : uint8
{
	/** The classic 2D windows. */
	Window,
	/** Holograms projected in the world. */
	Holo,
	/** What every client needs whichever style is in use (the HUD); fixed, never a choice. SWGUICore provides it. */
	Core
};

/** A UI feature the router can open in more than one presentation (or, for some, only one). */
UENUM(BlueprintType)
enum class ESWGUIFeature : uint8
{
	Inventory,
	PlanetMap,
	Survey,
	Crafting,
	Examine,
	WaypointList,
	Datapad,
	MissionBrowser,
	Travel,
	/** The structure placement host; holo placement views plug into it as modes. */
	StructurePlacement,
	/** The in-world HUD; only the Core presentation shows it. */
	Hud
};

/** What a presenter needs to open a feature. */
USTRUCT(BlueprintType)
struct SWGUICOMMON_API FSWGUIFeatureRequest
{
	GENERATED_BODY()

	/** Examine: the object. Missions: the terminal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|UI")
	int64 ObjectId = 0;

	/** Inventory: the gamepad dock form instead of the floating window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|UI")
	bool bDocked = false;

	/** The request came from using a tool (survey, crafting): an already open view just refreshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|UI")
	bool bFromTool = false;
};

/**
 * One presentation's implementation of the UI features, implemented by a LocalPlayerSubsystem in each visual plugin
 * and registered with USWGUISubsystem. The router never names a widget class; presenters never name each other.
 */
class SWGUICOMMON_API ISWGUIPresenter
{
public:
	virtual ~ISWGUIPresenter() = default;

	virtual ESWGUIPresentation GetPresentation() const = 0;
	virtual bool SupportsFeature(ESWGUIFeature Feature) const = 0;

	/** False when the feature exists but cannot open right now (the holo crafting stages it does not cover yet). */
	virtual bool CanOpenFeature(ESWGUIFeature Feature) const { return SupportsFeature(Feature); }

	virtual void OpenFeature(ESWGUIFeature Feature, const FSWGUIFeatureRequest& Request) = 0;
	virtual void CloseFeature(ESWGUIFeature Feature) = 0;
	virtual bool IsFeatureOpen(ESWGUIFeature Feature) const = 0;

	/** Open or close, as the key bound to the feature does; presenters override for form-specific rules. */
	virtual void ToggleFeature(ESWGUIFeature Feature)
	{
		if (IsFeatureOpen(Feature)) { CloseFeature(Feature); } else { OpenFeature(Feature, FSWGUIFeatureRequest()); }
	}

	/** The data behind an open view changed (the mission list). */
	virtual void NotifyDataChanged(ESWGUIFeature Feature) {}

	/** The radial menu closed; a gamepad dock takes focus back. */
	virtual void NotifyRadialMenuClosed() {}

	/** The HUD layer's opacity; holo views fade it so the hologram reads. Only the Core presenter owns a HUD. */
	virtual float GetHudOpacity() const { return 1.f; }
	virtual void SetHudOpacity(float Opacity) {}
};
