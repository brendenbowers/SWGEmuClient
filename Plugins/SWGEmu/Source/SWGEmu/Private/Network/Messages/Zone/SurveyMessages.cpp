#include "Network/Messages/Zone/SurveyMessages.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/SWGMessageRegistry.h"

REGISTER_SWG_MESSAGE(FResourceListForSurveyMessage, ESWGMessageOp::ResourceListForSurvey)
REGISTER_SWG_MESSAGE(FSurveyMessage, ESWGMessageOp::SurveyMessage)

bool FResourceListForSurveyMessage::Deserialize(FSWGMessage& Reader)
{
	uint32 Count = 0;
	Reader >> Count;
	if (Count > 10000)
	{
		return false;
	}
	Resources.SetNum(Count);
	for (FSWGSurveyResourceEntry& Entry : Resources)
	{
		Entry.Name = Reader.ReadAsciiString();
		Reader >> Entry.ObjectId;
		Entry.Type = Reader.ReadAsciiString();
	}
	SurveyType = Reader.ReadAsciiString();
	Reader >> PlayerId;
	return true;
}

bool FSurveyMessage::Deserialize(FSWGMessage& Reader)
{
	uint32 Count = 0;
	Reader >> Count;
	if (Count > 1000)
	{
		return false;
	}
	Points.SetNum(Count);
	for (FSWGSurveyPoint& Point : Points)
	{
		float Height = 0.f;
		Reader >> Point.X >> Height >> Point.Y >> Point.Density;
	}
	return true;
}
