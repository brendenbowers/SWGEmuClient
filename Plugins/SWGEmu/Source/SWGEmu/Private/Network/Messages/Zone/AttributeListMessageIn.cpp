#include "Network/Messages/Zone/AttributeListMessageIn.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FAttributeListMessageIn, ESWGMessageOp::AttributeListMessage)

bool FAttributeListMessageIn::Deserialize(FSWGMessage& Reader)
{
	int32 Count = 0;
	Reader >> ObjectId;
	Reader >> Count;

	// A hostile count can't make us allocate more than the packet can hold:
	// every entry is at least the two length prefixes.
	Attributes.Reserve(FMath::Clamp(Count, 0, Reader.GetRemaining() / 6));
	for (int32 Index = 0; Index < Count && Reader.GetRemaining() > 0; ++Index)
	{
		FSWGObjectAttribute& Attribute = Attributes.AddDefaulted_GetRef();
		Attribute.Name = Reader.ReadAsciiString();
		Attribute.Value = Reader.ReadUnicodeString();
	}
	return true;
}
