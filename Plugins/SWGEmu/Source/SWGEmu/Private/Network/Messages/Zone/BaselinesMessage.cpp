#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FBaselinesMessage, ESWGMessageOp::BaselinesMessage)

bool FBaselinesMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> ObjectId;
	Reader >> ObjectType;
	Reader >> BaselineType;
	Reader >> MessageSize;
	Reader >> OperationCount;

	// Whatever remains is the opaque per-object-type baseline field data.
	const int32 Remaining = Reader.GetRemaining();
	if (Remaining > 0)
	{
		RawPayload.SetNumUninitialized(Remaining);
		Reader.Serialize(RawPayload.GetData(), Remaining);
	}

	return true;
}

FString FBaselinesMessage::GetObjectTypeFourCC() const
{
	return SWGFourCCToString(ObjectType);
}
