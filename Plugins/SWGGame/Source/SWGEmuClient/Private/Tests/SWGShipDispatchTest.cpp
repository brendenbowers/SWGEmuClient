#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Objects/World/SWGShip.h"
#include "Subsystems/SWGBaselineHandlerRegistry.h"
#include "Subsystems/SWGDeltaHandlerRegistry.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/Zone/Object/JtlShipMessages.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGShipDispatchTest, "SWG.Space.ShipDispatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGShipDispatchTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ASWGShip* Ship = World->SpawnActor<ASWGShip>();
	if (!Ship) return false;
	FSWGPacket Payload;
	Payload.WriteInt32(0x76); Payload.WriteInt32(0); Payload.WriteUInt32(0); Payload.WriteUInt16(42);
	for (int32 Field = 0; Field < 9; ++Field) Payload.WriteFloat(10.f);
	Payload.WriteUInt64(0); Payload.WriteUInt32(0);
	for (int32 Field = 0; Field < 2; ++Field) { Payload.WriteInt32(0); Payload.WriteUInt32(0); }
	for (int32 Field = 0; Field < 4; ++Field) Payload.WriteAsciiString(TEXT(""));
	Payload.WriteFloat(0); Payload.WriteFloat(0); Payload.WriteUInt32(0);
	auto Envelope = [](FSWGPacket& Body, uint16 Count)
	{
		FSWGPacket P; P.WriteUInt16(5); P.WriteUInt32(0); P.WriteUInt64(123);
		P.WriteUInt32(uint32(ESWGObjectType::SHIP)); P.WriteByte(6);
		P.WriteUInt32(Body.Data.Num()+2); P.WriteUInt16(Count); P.Serialize(Body.Data.GetData(), Body.Data.Num());
		return P;
	};
	FSWGPacket BaselinePacket = Envelope(Payload, 23);
	FSWGMessage Reader(FSWGPacket(BaselinePacket.Data.GetData(), BaselinePacket.Data.Num()));
	FBaselinesMessage Baseline(0, Reader);
	TestTrue(TEXT("SHIP baseline dispatched to actual ship"), FSWGBaselineHandlerRegistry::Get().TryHandle(*Ship, Baseline, {}));
	TestEqual(TEXT("actor gets compact ID"), Ship->ShipState.ShipId, uint16(42));
	FSWGPacket Update; Update.WriteUInt16(11); Update.WriteFloat(125.f);
	FSWGPacket DeltaPacket = Envelope(Update, 1);
	FSWGMessage DeltaReader(FSWGPacket(DeltaPacket.Data.GetData(), DeltaPacket.Data.Num()));
	FDeltasMessage Delta(0, DeltaReader);
	TestTrue(TEXT("SHIP delta dispatched to actual ship"), FSWGDeltaHandlerRegistry::Get().TryHandle(*Ship, Delta, {}));
	TestEqual(TEXT("actor gets updated flight limit"), Ship->ShipState.Flight.MaxSpeed, 125.f);
	FSWGPacket ListPacket;
	ListPacket.WriteUInt32(3);
	ListPacket.WriteUInt64(900); ListPacket.WriteAsciiString(TEXT("Mos Eisley"));
	ListPacket.WriteUInt64(1001); ListPacket.WriteAsciiString(TEXT("Mos Eisley"));
	ListPacket.WriteUInt64(1002); ListPacket.WriteAsciiString(TEXT("Theed"));
	FJtlShipListResponse List;
	FSWGPacket ListReader(ListPacket.Data.GetData(), ListPacket.Data.Num());
	if (!TestTrue(TEXT("Core3 ship list payload decodes"), List.Deserialize(ListReader))) return false;
	TestEqual(TEXT("terminal entry is excluded from ship count"), List.Ships.Num(), 2);
	TestEqual(TEXT("terminal ID"), List.TerminalId, uint64(900));
	TestEqual(TEXT("parking location retained"), List.Ships[1].ParkingLocation, FString(TEXT("Theed")));
	FSWGPacket BadList; BadList.WriteUInt32(0xffffffff);
	FSWGPacket BadReader(BadList.Data.GetData(), BadList.Data.Num());
	TestFalse(TEXT("oversized ship list rejected"), List.Deserialize(BadReader));
	TestEqual(TEXT("failed ship list preserves previous data"), List.Ships.Num(), 2);
	FSWGPacket RequestBytes = FJtlShipListRequest(123, 900).Serialize();
	FSWGPacket Request(RequestBytes.Data.GetData(), RequestBytes.Data.Num());
	TestEqual(TEXT("ship list envelope count"), Request.ReadUInt16(), uint16(5));
	TestEqual(TEXT("ship list controller opcode"), Request.ReadUInt32(), uint32(0x80CE5E46));
	TestEqual(TEXT("ship list request priority"), Request.ReadUInt32(), uint32(0x0B));
	TestEqual(TEXT("ship list request subtype"), Request.ReadUInt32(), uint32(0x41C));
	TestEqual(TEXT("ship list requester"), Request.ReadUInt64(), uint64(123));
	TestEqual(TEXT("ignored request size field"), Request.ReadUInt32(), uint32(0));
	TestEqual(TEXT("ship list requested terminal"), Request.ReadUInt64(), uint64(900));
	return true;
}
#endif
