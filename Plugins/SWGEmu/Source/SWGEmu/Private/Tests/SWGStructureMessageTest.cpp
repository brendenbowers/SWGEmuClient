#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/Zone/StructureMessages.h"
#include "Network/Messages/Zone/ChatSystemMessage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGStructureModeParseTest,
	"SWGEmu.Messages.StructureMode.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGStructureModeParseTest::RunTest(const FString& Parameters)
{
	FSWGPacket Packet;
	Packet.WriteUInt16(3);
	Packet.WriteUInt32(0xE8A54DC1u);
	Packet.WriteUInt64(123456789ull);
	Packet.WriteAsciiString(TEXT("object/building/player/shared_player_house_tatooine_small_style_01.iff"));
	FSWGMessage Reader(Packet);
	FEnterStructurePlacementModeMessage Message(Reader.Opcode, Reader);
	TestEqual(TEXT("deed"), Message.DeedId, 123456789ull);
	TestEqual(TEXT("template"), Message.ClientTemplatePath,
		FString(TEXT("object/building/player/shared_player_house_tatooine_small_style_01.iff")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGParameterizedSystemMessageTest,
	"SWGEmu.Messages.ChatSystemMessage.Parameterized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGParameterizedSystemMessageTest::RunTest(const FString& Parameters)
{
	FSWGPacket Packet;
	Packet.WriteUInt16(4);
	Packet.WriteUInt32(0x6D2A6413u);
	Packet.WriteByte(0);
	Packet.WriteUnicodeString(TEXT(""));
	Packet.WriteUInt32(1); // parameter blob follows
	Packet.WriteUInt16(0);
	Packet.WriteByte(1);
	Packet.WriteUInt32(0xFFFFFFFFu);
	Packet.WriteAsciiString(TEXT("player_structure"));
	Packet.WriteUInt32(0);
	Packet.WriteAsciiString(TEXT("not_permitted"));
	FSWGMessage Reader(Packet);
	FChatSystemMessage Message(Reader.Opcode, Reader);
	TestEqual(TEXT("string ID"), Message.Message, FString(TEXT("@player_structure:not_permitted")));
	return true;
}

#endif
