#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/Zone/SurveyMessages.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSurveyMessagesParseTest,
	"SWGEmu.Messages.Survey.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGSurveyMessagesParseTest::RunTest(const FString& Parameters)
{
	// Byte order as Core3's ResourceListForSurveyMessage::addResource/finish write it.
	FSWGPacket ListPacket;
	ListPacket.WriteUInt16(4);
	ListPacket.WriteUInt32(0x8A64B1D5u);
	ListPacket.WriteUInt32(2);
	ListPacket.WriteAsciiString(TEXT("Aqabuo"));
	ListPacket.WriteUInt64(1234);
	ListPacket.WriteAsciiString(TEXT("copper_desh"));
	ListPacket.WriteAsciiString(TEXT("Oroxin"));
	ListPacket.WriteUInt64(5678);
	ListPacket.WriteAsciiString(TEXT("iron_polonium"));
	ListPacket.WriteAsciiString(TEXT("mineral"));
	ListPacket.WriteUInt64(99);

	FSWGMessage ListReader(ListPacket);
	FResourceListForSurveyMessage List(ListReader.Opcode, ListReader);
	if (TestEqual(TEXT("resource count"), List.Resources.Num(), 2))
	{
		TestEqual(TEXT("name"), List.Resources[0].Name, FString(TEXT("Aqabuo")));
		TestEqual(TEXT("object id"), List.Resources[0].ObjectId, 1234ull);
		TestEqual(TEXT("type"), List.Resources[1].Type, FString(TEXT("iron_polonium")));
	}
	TestEqual(TEXT("survey type"), List.SurveyType, FString(TEXT("mineral")));
	TestEqual(TEXT("player id"), List.PlayerId, 99ull);

	FSWGPacket SurveyPacket;
	SurveyPacket.WriteUInt16(2);
	SurveyPacket.WriteUInt32(0x877F79ACu);
	SurveyPacket.WriteUInt32(2);
	SurveyPacket.WriteFloat(-32.f); SurveyPacket.WriteFloat(0.f); SurveyPacket.WriteFloat(64.f); SurveyPacket.WriteFloat(0.25f);
	SurveyPacket.WriteFloat(32.f); SurveyPacket.WriteFloat(0.f); SurveyPacket.WriteFloat(64.f); SurveyPacket.WriteFloat(0.75f);

	FSWGMessage SurveyReader(SurveyPacket);
	FSurveyMessage Survey(SurveyReader.Opcode, SurveyReader);
	if (TestEqual(TEXT("point count"), Survey.Points.Num(), 2))
	{
		TestEqual(TEXT("x"), Survey.Points[1].X, 32.f);
		TestEqual(TEXT("y skips the zero height"), Survey.Points[1].Y, 64.f);
		TestEqual(TEXT("density"), Survey.Points[1].Density, 0.75f);
	}
	return true;
}

#endif
