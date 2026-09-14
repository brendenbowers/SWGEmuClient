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
};

/**
 * Parser for the retail UI .inc format: an XML-like tree of <Namespace>,
 * <ImageStyle>, <RectangleStyle>, ... elements with single-quoted attributes
 * that may each sit on their own line. Only Namespace nesting and ImageStyle
 * leaves are kept; everything else is skipped structurally.
 */
class SWGTRE_API FSWGUIStyleReader
{
public:
	static bool Read(const TArray<uint8>& Data, FSWGUIStyleSheet& OutSheet);

private:
	FSWGUIStyleReader() = default;
};
