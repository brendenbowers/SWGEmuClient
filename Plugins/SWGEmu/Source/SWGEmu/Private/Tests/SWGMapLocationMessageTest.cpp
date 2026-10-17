#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/Zone/MapLocationMessages.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGMapLocationMessageTest,
	"SWGEmu.MapLocations.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGMapLocationMessageTest::RunTest(const FString& Parameters)
{
	FSWGPacket Request = FGetMapLocationsRequestMessage{ TEXT("tatooine") }.Serialize();
	Request.SetReadMode();
	TestEqual(TEXT("request operand count"), Request.ReadUInt16(), static_cast<uint16>(2));
	TestEqual(TEXT("request opcode"), Request.ReadUInt32(), 0x1A7AB839u);
	TestEqual(TEXT("requested planet"), Request.ReadAsciiString(), FString(TEXT("tatooine")));

	FSWGPacket Packet;
	Packet.WriteUInt16(5);
	Packet.WriteUInt32(0x9F80464Cu);
	Packet.WriteAsciiString(TEXT("tatooine"));
	Packet.WriteUInt32(2);
	Packet.WriteUInt64(101);
	Packet.WriteUnicodeString(TEXT("Mos Eisley"));
	Packet.WriteFloat(3500.f);
	Packet.WriteFloat(-4800.f);
	Packet.WriteByte(3);
	Packet.WriteByte(0);
	Packet.WriteByte(0);
	Packet.WriteUInt64(102);
	Packet.WriteUnicodeString(TEXT("Mission Terminal"));
	Packet.WriteFloat(3510.f);
	Packet.WriteFloat(-4810.f);
	Packet.WriteByte(41);
	Packet.WriteByte(44);
	Packet.WriteByte(1);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Packet.WriteUInt32(0);
	}

	FSWGMessage Reader(Packet);
	FGetMapLocationsResponseMessage Response(Reader.Opcode, Reader);
	TestTrue(TEXT("valid response"), Response.bValid);
	if (TestEqual(TEXT("location count"), Response.Locations.Num(), 2))
	{
		TestEqual(TEXT("cantina name"), Response.Locations[0].Name, FString(TEXT("Mos Eisley")));
		TestEqual(TEXT("world north coordinate"), Response.Locations[0].Position.Y, -4800.0);
		TestEqual(TEXT("terminal category"), Response.Locations[1].Category, static_cast<uint8>(41));
		TestEqual(TEXT("terminal subcategory"), Response.Locations[1].Subcategory, static_cast<uint8>(44));
	}
	return true;
}

#endif
