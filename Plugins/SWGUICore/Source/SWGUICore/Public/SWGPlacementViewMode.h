#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "InputCoreTypes.h"
#include "SWGPlacementViewMode.generated.h"

class UUserWidget;
class FSWGCameraTakeover;

/**
 * An alternative way of placing a structure, hosted by the placement widget: it takes over the camera and the
 * input while active (the holo datapad and the holo map are two). The widget owns the camera (FSWGCameraTakeover) and
 * offers the active mode every key and mouse event first; whatever the mode does not consume the widget handles
 * (rotate, place, cancel). Modes come from SWGUIHolo and register in FSWGPlacementViewRegistry, so the widget
 * never names one.
 */
UCLASS(Abstract)
class SWGUICORE_API USWGPlacementViewMode : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnStateChanged);
	FOnStateChanged OnStateChanged;

	virtual bool Begin(UUserWidget* InOwner, FSWGCameraTakeover* InView) PURE_VIRTUAL(USWGPlacementViewMode::Begin, return false;);
	virtual void End() PURE_VIRTUAL(USWGPlacementViewMode::End, );
	virtual bool IsActive() const PURE_VIRTUAL(USWGPlacementViewMode::IsActive, return false;);
	virtual void Tick(float DeltaTime) {}

	/** True when the mode consumed the event. */
	virtual bool HandleKeyDown(const FKey& Key, bool bShift, bool bControl) { return false; }
	virtual bool HandleKeyUp(const FKey& Key) { return false; }
	virtual bool HandleMouseButtonDown(const FKey& Button) { return false; }
	virtual bool HandleMouseButtonUp(const FKey& Button) { return false; }
	virtual bool HandleMouseMove(const FVector2D& CursorDelta) { return false; }
	virtual bool HandleMouseWheel(float Delta) { return false; }
	virtual void HandleFocusLost() {}

	/** A named action from outside the widget (a console command), such as "FineTune". True if the mode took it. */
	virtual bool HandleCommand(FName Command) { return false; }

	/** The mode steers the world camera by itself, so the widget leaves keys and the mouse to the game (fine tune). */
	virtual bool PassesInputToWorld() const { return false; }

	/** A press of this button starts a drag, so the widget keeps the mouse until it is released. */
	virtual bool WantsMouseCapture(const FKey& Button) const { return false; }

	/** Short phrase for the status line ("Camera droid"); empty for none. */
	virtual FString GetStatusDetail() const { return FString(); }

	/** Key prompts shown while the mode is in a state that needs them; empty hides the prompt. */
	virtual FString GetPrompt() const { return FString(); }
};

/** How one placement view appears in the host: its id, the key that toggles it, and its class. */
struct FSWGPlacementViewRegistration
{
	FName Id;
	FText Label;
	FKey ToggleKey;
	TSubclassOf<USWGPlacementViewMode> ModeClass;
};

/** Where the visual plugins register their placement views; the placement widget builds its keys from here. */
class SWGUICORE_API FSWGPlacementViewRegistry
{
public:
	static void Register(const FSWGPlacementViewRegistration& Registration);
	static void Unregister(FName Id);
	static const TArray<FSWGPlacementViewRegistration>& GetAll();

	/** Console and tests ask an open placement host to toggle a view, or send its active mode a command. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnToggleRequested, FName /*ModeId*/);
	static FOnToggleRequested& OnToggleRequested();
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnCommandRequested, FName /*ModeId*/, FName /*Command*/);
	static FOnCommandRequested& OnCommandRequested();
};
