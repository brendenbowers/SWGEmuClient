#include "Misc/AutomationTest.h"

#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/ServerWeatherMessage.h"
#include "Network/SWGPacket.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGWeatherMessageParseTest,
	"SWGEmu.Messages.Weather.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSWGWeatherMessageParseTest::RunTest(const FString& Parameters)
{
	// Little-endian bytes mirroring Core3's ServerWeatherMessage inserts.
	TArray<uint8> Bytes;
	auto AddU32 = [&Bytes](uint32 Value)
	{
		for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
		{
			Bytes.Add((Value >> (8 * ByteIndex)) & 0xFF);
		}
	};
	auto AddFloat = [&AddU32](float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, 4);
		AddU32(Bits);
	};
	Bytes.Add(3);
	Bytes.Add(0);
	AddU32(0x486356EAu);
	AddU32(3);
	AddFloat(0.25f);
	AddFloat(-0.5f);
	AddFloat(-0.75f);

	FSWGPacket Packet(Bytes.GetData(), Bytes.Num());
	FSWGMessage Reader(Packet);
	TestEqual(TEXT("weather opcode"), Reader.Opcode, static_cast<uint32>(ESWGMessageOp::ServerWeatherMessage));
	FServerWeatherMessage Weather(Reader.Opcode, Reader);
	TestEqual(TEXT("weather index"), Weather.Weather, 3);
	TestEqual(TEXT("wind x"), Weather.WindX, 0.25f);
	TestEqual(TEXT("wind up"), Weather.WindUp, -0.5f);
	TestEqual(TEXT("wind z"), Weather.WindZ, -0.75f);
	return true;
}

#endif
