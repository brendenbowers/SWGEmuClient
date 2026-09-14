#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

/**
 * The player acting on a SUI window (opcode 0x092D3564, opcount 2): which
 * subscription fired and the values it asked for.
 *
 * EventIndex is the position of the SubscribeToEvent command in the page's
 * command list — for the stock boxes 0 is OK and 1 is Cancel. Arguments are
 * the current values of that subscription's (widget, property) pairs, in
 * order: the selected row for a list box, the typed text for an input box.
 *
 * Wire layout (Core3 SuiEventNotificationCallback::parse):
 *   [0x02][0x092D3564] pageId(int32) eventIndex(int32) count(int32) count(int32) unicode*
 */
struct SWGEMU_API FSuiEventNotificationMessage
{
	uint32 PageId = 0;
	uint32 EventIndex = 0;
	TArray<FString> Arguments;

	FSWGPacket Serialize() const;
};
