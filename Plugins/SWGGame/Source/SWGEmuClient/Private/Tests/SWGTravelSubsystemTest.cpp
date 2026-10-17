#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGTravelSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGTravelPurchaseArgumentsTest,
	"SWG.Travel.PurchaseArguments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGTravelPurchaseArgumentsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("spaces are encoded for Core3's whitespace tokenizer"),
		USWGTravelSubsystem::BuildPurchaseArguments(TEXT("tatooine"), TEXT("Mos Eisley Starport"),
			TEXT("yavin4"), TEXT("Yavin IV Mining Outpost"), false),
		FString(TEXT("tatooine Mos_Eisley_Starport yavin4 Yavin_IV_Mining_Outpost single")));
	TestEqual(TEXT("round trip uses a non-single trip token"),
		USWGTravelSubsystem::BuildPurchaseArguments(TEXT("naboo"), TEXT("Theed Spaceport"),
			TEXT("corellia"), TEXT("Coronet Starport"), true),
		FString(TEXT("naboo Theed_Spaceport corellia Coronet_Starport round")));
	return true;
}

/** The server's terminal reply, dispatched as the network subsystem does, must reach the travel subsystem. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGTravelEnterPurchaseModeTest,
	"SWG.Travel.EnterPurchaseMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWGTravelEnterPurchaseModeTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	ON_SCOPE_EXIT { GameInstance->Shutdown(); };

	USWGNetworkSubsystem* Network = GameInstance->GetSubsystem<USWGNetworkSubsystem>();
	USWGTravelSubsystem* Travel = GameInstance->GetSubsystem<USWGTravelSubsystem>();
	if (!TestNotNull(TEXT("network subsystem"), Network) || !TestNotNull(TEXT("travel subsystem"), Travel))
	{
		return false;
	}

	// Core3's EnterTicketPurchaseModeMessage: opcount 4, CRC, planet, point name, zero byte.
	FSWGPacket Packet;
	Packet.WriteUInt16(0x04);
	Packet.WriteUInt32(0x904DAE1Au);
	Packet.WriteAsciiString(TEXT("rori"));
	Packet.WriteAsciiString(TEXT("Narmle Starport"));
	Packet.WriteByte(0);

	FSWGMessage Reader(Packet);
	TSharedPtr<FSWGNetMessage> Message = FSWGMessageRegistry::Get().Create(Reader.Opcode, Reader);
	if (!TestTrue(TEXT("opcode is registered"), Message.IsValid()))
	{
		return false;
	}
	Network->OnMessageReceived.Broadcast(Message);

	TestEqual(TEXT("departure planet"), Travel->GetDeparturePlanet(), FString(TEXT("rori")));
	TestEqual(TEXT("departure location"), Travel->GetDepartureLocation(), FString(TEXT("Narmle Starport")));
	return true;
}

#endif
