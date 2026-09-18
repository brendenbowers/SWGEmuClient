#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/** One line of an examine window: a key the client names ("@obj_attr_n:condition") and the server's text for it. */
struct SWGEMU_API FSWGObjectAttribute
{
	/** Attribute key, e.g. "condition", "volume", "crafter". Shown through obj_attr_n.stf. */
	FString Name;

	/** Already-composed text; may itself be an "@table:key" reference. */
	FString Value;
};

/**
 * AttributeListMessage (0xF3F12F2A) — the reply to the getattributesbatch
 * command: everything an examine window lists for one object.
 *
 * Payload (Core3 AttributeListMessage):
 *   objectId(int64) count(int32) count x { name(ascii) value(unicode) }
 */
struct SWGEMU_API FAttributeListMessageIn : public FSWGNetMessage
{
	int64 ObjectId = 0;
	TArray<FSWGObjectAttribute> Attributes;

	FAttributeListMessageIn(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
