#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/Zone/TravelMessages.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGPlanetTravelPointListParseTest,
	"SWGEmu.Messages.PlanetTravelPointList.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGPlanetTravelPointListParseTest::RunTest(const FString& Parameters)
{
	FSWGPacket Request = FPlanetTravelPointListRequestMessage{ TEXT("naboo") }.Serialize();
	Request.SetReadMode();
	TestEqual(TEXT("request operand count"), Request.ReadUInt16(), static_cast<uint16>(3));
	TestEqual(TEXT("request opcode"), Request.ReadUInt32(), 0x96405D4Du);
	TestEqual(TEXT("ignored player id"), Request.ReadUInt64(), 0ull);
	TestEqual(TEXT("requested planet"), Request.ReadAsciiString(), FString(TEXT("naboo")));

	FSWGPacket Packet;
	Packet.WriteUInt16(0x06);
	Packet.WriteUInt32(0x4D32541Fu);
	Packet.WriteAsciiString(TEXT("corellia"));
	Packet.WriteUInt32(2);
	Packet.WriteAsciiString(TEXT("Coronet Starport"));
	Packet.WriteAsciiString(TEXT("Kor Vella Starport"));
	Packet.WriteUInt32(2);
	Packet.WriteFloat(-137.f); Packet.WriteFloat(28.f); Packet.WriteFloat(-4723.f);
	Packet.WriteFloat(-3137.f); Packet.WriteFloat(30.f); Packet.WriteFloat(2808.f);
	Packet.WriteUInt32(2);
	Packet.WriteUInt32(25); Packet.WriteUInt32(0);
	Packet.WriteUInt32(2);
	Packet.WriteByte(1); Packet.WriteByte(1);

	FSWGMessage Reader(Packet);
	FPlanetTravelPointListResponseMessage Response(Reader.Opcode, Reader);
	TestEqual(TEXT("planet"), Response.Planet, FString(TEXT("corellia")));
	if (TestEqual(TEXT("point count"), Response.Points.Num(), 2))
	{
		TestEqual(TEXT("exact purchase name"), Response.Points[0].Name, FString(TEXT("Coronet Starport")));
		TestEqual(TEXT("map y uses world y"), Response.Points[0].Y, -4723.f);
		TestEqual(TEXT("tax"), Response.Points[0].Tax, 25u);
		TestTrue(TEXT("interplanetary"), Response.Points[0].bInterplanetary);
	}
	return true;
}

#endif
