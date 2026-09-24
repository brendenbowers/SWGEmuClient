#include "Network/Messages/Zone/MapLocationMessages.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/SWGMessageRegistry.h"

REGISTER_SWG_MESSAGE(FGetMapLocationsResponseMessage, ESWGMessageOp::GetMapLocationsResponse)

bool FGetMapLocationsResponseMessage::Deserialize(FSWGMessage& Reader)
{
	Planet = Reader.ReadAsciiString().ToLower();
	if (Planet.IsEmpty() || Reader.GetRemaining() < 4)
	{
		return false;
	}
	const uint32 Count = Reader.ReadUInt32();
	// Each entry has at least an id, an empty Unicode name, two coordinates and three category bytes.
	if (Count > 10000 || Count > static_cast<uint32>(Reader.GetRemaining() / 23))
	{
		return false;
	}
	Locations.Reserve(Count);
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		if (Reader.GetRemaining() < 23)
		{
			return false;
		}
		FSWGMapLocation& Location = Locations.AddDefaulted_GetRef();
		Location.ObjectId = Reader.ReadUInt64();
		Location.Name = Reader.ReadUnicodeString();
		if (Reader.GetRemaining() < 11)
		{
			return false;
		}
		Location.Position.X = Reader.ReadFloat();
		Location.Position.Y = Reader.ReadFloat();
		Location.Category = Reader.ReadByte();
		Location.Subcategory = Reader.ReadByte();
		Location.Icon = Reader.ReadByte();
	}
	return Reader.GetRemaining() >= 20; // Core3 appends five empty/unknown uint32 fields.
}
