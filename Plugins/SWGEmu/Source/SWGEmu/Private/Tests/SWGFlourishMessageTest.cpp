#include "Misc/AutomationTest.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/Object/FlourishIn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGFlourishParseTest,
	"SWGEmu.Messages.Flourish.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::SmokeFilter)

bool FSWGFlourishParseTest::RunTest(const FString& Parameters)
{
	auto ParseFlourish = [](int32 Value, FFlourishIn& Out, bool& bOutAtEnd)
	{
		// Little-endian int32 built byte by byte, as the other message tests do.
		const uint32 Bits = static_cast<uint32>(Value);
		const uint8 Bytes[4] = { static_cast<uint8>(Bits & 0xFF), static_cast<uint8>((Bits >> 8) & 0xFF), static_cast<uint8>((Bits >> 16) & 0xFF), static_cast<uint8>((Bits >> 24) & 0xFF) };
		FSWGPacket Packet(Bytes, 4);
		const bool bParsed = Out.Parse(Packet);
		bOutAtEnd = Packet.IsAtEnd();
		return bParsed;
	};

	FFlourishIn Flourish;
	bool bAtEnd = false;
	TestTrue(TEXT("parses flourish 3"), ParseFlourish(3, Flourish, bAtEnd));
	TestEqual(TEXT("flourish id"), Flourish.FlourishId, 3);
	TestTrue(TEXT("is a flourish"), Flourish.IsFlourish());
	TestTrue(TEXT("consumed the whole payload"), bAtEnd);

	// Core3 sends -1 when the performer asks to stop.
	FFlourishIn Terminator;
	TestTrue(TEXT("parses -1"), ParseFlourish(-1, Terminator, bAtEnd));
	TestEqual(TEXT("terminator id"), Terminator.FlourishId, -1);
	TestFalse(TEXT("-1 is not a flourish"), Terminator.IsFlourish());

	const uint8 ShortBytes[2] = { 1, 0 };
	FSWGPacket ShortPacket(ShortBytes, 2);
	FFlourishIn Truncated;
	TestFalse(TEXT("rejects a two-byte payload"), Truncated.Parse(ShortPacket));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
