#pragma once

#include "CoreMinimal.h"

/**
 * One <ImageStyle> from a ui/*.inc style sheet: a rectangle cut from a
 * texture/<Source>.dds sprite sheet. SourceRect is x0,y0,x1,y1 in pixels
 * with the max edge exclusive (the toolbar icons are all 24x24).
 */
struct SWGTRE_API FSWGUIImageStyle
{
	/** Bare texture name, e.g. "ui_rebel_icons" - any "ui_shader_add:" prefix already stripped. */
	FString Source;
	FIntRect SourceRect;
};

/**
 * The image styles of a ui/*.inc file, addressed by their dotted namespace
 * path below the root, lowercased ("icon.command.kneel").
 *
 * Retail's UI namespaces also carry attribute aliases that point one name
 * at another style ("stand='/styles.icon.posture.upright'" on the
 * icon.command namespace) - FindImageStyle follows those, so callers ask
 * for the command name and get whichever style the sheet ends up at.
 */
struct SWGTRE_API FSWGUIStyleSheet
{
	TMap<FString, FSWGUIImageStyle> ImageStyles;

	/** Lowercased dotted path -> lowercased dotted path of the style it aliases. */
	TMap<FString, FString> Aliases;

	const FSWGUIImageStyle* FindImageStyle(const FString& DottedPath) const;

	/**
	 * Sound attributes of styled elements (ButtonStyle MouseOverSound / ReleaseSound, TextboxStyle
	 * SoundKeyclick), keyed by the element's lowercased dotted path ("new.buttons.hud.style"), then by
	 * lowercased attribute name. Values are the sheet's own names: "ui_rollover" means sound/ui_rollover.snd.
	 */
	TMap<FString, TMap<FString, FString>> ElementSounds;

	/** The sound named by Attribute on the element at DottedPath, or nullptr. */
	const FString* FindElementSound(const FString& DottedPath, const FString& Attribute) const;
};

/**
 * Parser for the retail UI .inc format: an XML-like tree of <Namespace>,
 * <ImageStyle>, <RectangleStyle>, ... elements with single-quoted attributes
 * that may each sit on their own line. Only Namespace nesting, ImageStyle
 * leaves and sound attributes are kept; everything else is skipped structurally.
 */
class SWGTRE_API FSWGUIStyleReader
{
public:
	static bool Read(const TArray<uint8>& Data, FSWGUIStyleSheet& OutSheet);

private:
	FSWGUIStyleReader() = default;
};
