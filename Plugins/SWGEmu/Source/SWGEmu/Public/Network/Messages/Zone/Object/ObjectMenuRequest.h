#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/Zone/ObjectControllerMessage.h"

/**
 * One radial menu entry, in the shape both directions use: the client offers
 * its defaults in ObjectMenuRequest and the server hands the merged menu back
 * in ObjectMenuResponse (Core3 RadialMenuItem / RadialClientItem).
 *
 * Index is the item's 1-based position in the flattened list and ParentIndex
 * the Index of the item it nests under (0 = top level). RadialId is the
 * option's meaning (datatables/player/radial_menu.iff row, Core3
 * RadialOptions), Callback its flags — 3 means "tell the server when picked".
 */
struct SWGEMU_API FSWGRadialMenuEntry
{
	uint8 Index = 0;
	uint8 ParentIndex = 0;
	uint8 RadialId = 0;
	uint8 Callback = 0;

	/** Label, usually a "@ui_radial:key" reference; empty for standard options the client names itself. */
	FString Text;

	bool NotifiesServer() const { return (Callback & 2) != 0; }
};

/**
 * Asks the server for an object's radial menu (sub-opcode 0x146). The reply is
 * an ObjectMenuResponse carrying Counter back so it can be matched.
 *
 * Payload (Core3 ObjectMenuRequestCallback::parse):
 *   size(int32, unused) targetId(int64) playerId(int64)
 *   count(int32) { index(u8) parent(u8) radialId(u8) callback(u8) text(unicode) }*
 *   counter(u8)
 */
struct SWGEMU_API FObjectMenuRequest : public FObjectControllerMessage
{
	uint64 TargetId = 0;
	uint8 Counter = 0;
	TArray<FSWGRadialMenuEntry> ClientItems;

	FObjectMenuRequest(uint64 PlayerId, uint64 TargetId, uint8 Counter);

	FSWGPacket Serialize() const;
};
