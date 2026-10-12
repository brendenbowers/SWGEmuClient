#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SWGUISound.generated.h"

class UButton;
class USWGTreSubsystem;

/** UI sound names as retail's data gives them; every value is a sound path SWGAudio accepts. */
struct SWGUICOMMON_API FSWGUISoundNames
{
	/** MouseOverSound / ReleaseSound of the default hud button style (ui_styles.inc). */
	FString Hover;
	FString Click;

	/** SoundKeyclick of the chat textbox style. */
	FString Keyclick;

	/** datatables/player/sounds.iff: lowercased name ("radial_create") to .snd path. */
	TMap<FString, FString> Named;

	/** Reads the style sheet and sounds.iff; names the data lacks fall back to retail's usual ones. */
	static FSWGUISoundNames Resolve(USWGTreSubsystem* Tre);
};

/**
 * Plays retail's UI sounds on the 2D UI bus. The names come from the TRE (see FSWGUISoundNames)
 * and are resolved once per TRE subsystem. WorldContext is any object in the game world.
 */
namespace SWGUISound
{
	SWGUICOMMON_API const FSWGUISoundNames& GetNames(const UObject* WorldContext);

	/** Button mouse-over, retail's MouseOverSound. */
	SWGUICOMMON_API void PlayHover(const UObject* WorldContext);

	/** Button release, retail's ReleaseSound. */
	SWGUICOMMON_API void PlayClick(const UObject* WorldContext);

	/** Textbox keystroke, retail's SoundKeyclick. */
	SWGUICOMMON_API void PlayKeyclick(const UObject* WorldContext);

	/** A sounds.iff name ("radial_create", "backpack_open"); anything else is tried as a sound path. */
	SWGUICOMMON_API void PlayNamed(const UObject* WorldContext, const FString& Name);

	/** Hover and click sounds on a button that isn't a retail hud button (those already have them). */
	SWGUICOMMON_API void BindButton(UButton* Button);
}

/** Plays a button's hover and click sounds; created by SWGUISound::BindButton. */
UCLASS()
class SWGUICOMMON_API USWGUIButtonSound : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UButton> Button;

	UFUNCTION() void HandleHovered();
	UFUNCTION() void HandleClicked();
};
