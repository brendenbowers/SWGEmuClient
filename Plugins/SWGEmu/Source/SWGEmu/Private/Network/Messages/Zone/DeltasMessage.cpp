#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FDeltasMessage, ESWGMessageOp::DeltasMessage)

bool FDeltasMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> ObjectId;
	Reader >> ObjectType;
	Reader >> DeltaType;
	Reader >> MessageSize;
	Reader >> UpdateCount;

	// Whatever remains is the opaque sequence of [fieldIndex][value] updates.
	const int32 Remaining = Reader.GetRemaining();
	if (Remaining > 0)
	{
		RawUpdates.SetNumUninitialized(Remaining);
		Reader.Serialize(RawUpdates.GetData(), Remaining);
	}

	return true;
}

FString FDeltasMessage::GetObjectTypeFourCC() const
{
	return SWGFourCCToString(ObjectType);
}
