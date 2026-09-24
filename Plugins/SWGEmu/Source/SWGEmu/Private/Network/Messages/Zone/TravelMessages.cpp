#include "Network/Messages/Zone/TravelMessages.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/SWGMessageRegistry.h"

REGISTER_SWG_MESSAGE(FEnterTicketPurchaseModeMessage, ESWGMessageOp::EnterTicketPurchaseMode)
REGISTER_SWG_MESSAGE(FPlanetTravelPointListResponseMessage, ESWGMessageOp::PlanetTravelPointListResponse)

bool FEnterTicketPurchaseModeMessage::Deserialize(FSWGMessage& Reader)
{
	DeparturePlanet = Reader.ReadAsciiString();
	DepartureLocation = Reader.ReadAsciiString();
	if (Reader.GetRemaining() > 0)
	{
		Reader.ReadByte();
	}
	return !DeparturePlanet.IsEmpty() && !DepartureLocation.IsEmpty();
}

bool FPlanetTravelPointListResponseMessage::Deserialize(FSWGMessage& Reader)
{
	Planet = Reader.ReadAsciiString();
	uint32 NameCount = 0;
	Reader >> NameCount;
	if (Planet.IsEmpty() || NameCount > 10000)
	{
		return false;
	}

	Points.SetNum(NameCount);
	for (FSWGPlanetTravelPoint& Point : Points)
	{
		Point.Name = Reader.ReadAsciiString();
	}

	uint32 CoordinateCount = 0;
	Reader >> CoordinateCount;
	if (CoordinateCount != NameCount)
	{
		return false;
	}
	for (FSWGPlanetTravelPoint& Point : Points)
	{
		Reader >> Point.X >> Point.Z >> Point.Y;
	}

	uint32 TaxCount = 0;
	Reader >> TaxCount;
	if (TaxCount != NameCount)
	{
		return false;
	}
	for (FSWGPlanetTravelPoint& Point : Points)
	{
		Reader >> Point.Tax;
	}

	uint32 InterplanetaryCount = 0;
	Reader >> InterplanetaryCount;
	if (InterplanetaryCount != NameCount)
	{
		return false;
	}
	for (FSWGPlanetTravelPoint& Point : Points)
	{
		uint8 Interplanetary = 0;
		Reader >> Interplanetary;
		Point.bInterplanetary = Interplanetary != 0;
	}
	return true;
}
