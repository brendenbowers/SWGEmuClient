#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Network/Objects/Zone/Player/PlayerObjectDelta.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGWaypointDeltaSetTest,
	"SWGEmu.Messages.PlayerDelta8.WaypointSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGWaypointDeltaSetTest::RunTest(const FString& Parameters)
{
	// Core3 PlayerObjectImplementation::updateWaypoint -> WaypointList::update:
	// field index, list header, op 2, map key, then WaypointObject::insertToMessage.
	FSWGPacket Packet;
	Packet.WriteUInt16(0x01);
	Packet.WriteInt32(1);
	Packet.WriteUInt32(7);
	Packet.WriteByte(2);
	Packet.WriteUInt64(281475002792558ull);
	Packet.WriteInt32(0);
	Packet.WriteFloat(-4643.6f);
	Packet.WriteFloat(0.f);
	Packet.WriteFloat(-1749.4f);
	Packet.WriteUInt64(0);
	Packet.WriteUInt32(0x51D50A39u);
	Packet.WriteUnicodeString(TEXT("Resource Survey"));
	Packet.WriteUInt64(281475002792558ull);
	Packet.WriteByte(1);
	Packet.WriteByte(1);
	Packet.SetReadMode();

	FPlayerObjectDelta Delta;
	SWGPlayerDeltaParser::ParseDelta8(Packet, Delta, 1);
	if (TestEqual(TEXT("one change"), Delta.WaypointList.Changes.Num(), 1))
	{
		const FWaypoint& Waypoint = Delta.WaypointList.Changes[0].Value;
		TestEqual(TEXT("map key"), Waypoint.MapKey, 281475002792558ull);
		TestEqual(TEXT("object id"), Waypoint.WaypointObjectId, 281475002792558ull);
		TestEqual(TEXT("x"), Waypoint.XCoord, -4643.6f);
		TestEqual(TEXT("y"), Waypoint.YCoord, -1749.4f);
		TestEqual(TEXT("name"), Waypoint.WaypointName, FString(TEXT("Resource Survey")));
		TestEqual(TEXT("active"), Waypoint.Active, static_cast<uint8>(1));
	}
	return true;
}

#endif
