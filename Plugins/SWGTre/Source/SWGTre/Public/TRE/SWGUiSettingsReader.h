#pragma once

#include "CoreMinimal.h"

/** One retail toolbar slot. Slots the file doesn't mention are simply absent. */
struct SWGTRE_API FSWGToolbarSlot
{
	int32 Pane = 0;
	int32 Slot = 0;

	/** "Command" for a slash command, otherwise the kind of object dragged in. */
	FString Type;

	/** The slash line as typed: "/burstRun", "/mood sad", "/macro heal". */
	FString Text;

	/** Object id for a dragged-in item, 0 for a command. */
	int64 ObjectId = 0;

	bool IsCommand() const { return Type == TEXT("Command"); }
};

/**
 * Reads the toolbar out of the retail client's per-character UI settings
 * file, profiles/<account>/<galaxy>/<oid>.uis — the only place SWG keeps it;
 * the server never sees the toolbar.
 *
 * Confirmed by hex dump: FORM UIST { FORM 0003 { RESO, FORM OWNE... } }, one
 * FORM OWNE per UI page: NAME (page class, "SwgCuiToolbar"), SIZE, LOCA, then
 * FORM STRS / FORM DATS / FORM INTS of DATA records, each
 *   name\0  int32 length (little-endian, unlike the chunk sizes)  bytes
 * The toolbar's DATS records are itemType_<pane>_<slot>, itemStr_<pane>_<slot>
 * and itemId_<pane>_<slot>; INTS holds numPanes and numItems_<pane>.
 */
class SWGTRE_API FSWGUiSettingsReader
{
public:
	/** Toolbar slots in the file, in file order. False if it isn't a UIST file or has no toolbar page. */
	static bool ReadToolbar(const TArray<uint8>& Data, TArray<FSWGToolbarSlot>& OutSlots);

	/** profiles/<account>/<galaxy>/<oid>.uis under the client install. */
	static FString MakeUiSettingsPath(const FString& ClientDirectory, const FString& Account, const FString& Galaxy, int64 CharacterId);
};
