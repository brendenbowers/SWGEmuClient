#include "Misc/AutomationTest.h"
#include "Network/Objects/Zone/Ship/ShipObjectBaseline.h"
#include "String/HexToBytes.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGShipBaselineTest, "SWG.Space.ShipBaselines", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGShipBaselineTest::RunTest(const FString& Parameters)
{
	FSWGPacket Fixture;
	Fixture.WriteInt32(0x76); Fixture.WriteInt32(0); Fixture.WriteUInt32(7);
	Fixture.WriteUInt16(42);
	for (int32 Value = 1; Value <= 9; ++Value) Fixture.WriteFloat(float(Value));
	Fixture.WriteUInt64(123); Fixture.WriteUInt32(5);
	Fixture.WriteInt32(1); Fixture.WriteUInt32(8); Fixture.WriteByte(0xa5);
	Fixture.WriteInt32(1); Fixture.WriteUInt32(9); Fixture.WriteByte(0); Fixture.WriteUInt32(2); Fixture.WriteUInt32(0x12345678);
	for (const FString& Value : { FString(TEXT("wing")), FString(TEXT("yacht")), FString(TEXT("easy")), FString(TEXT("neutral")) }) Fixture.WriteAsciiString(Value);
	Fixture.WriteFloat(10); Fixture.WriteFloat(20); Fixture.WriteUInt32(30);
	FSWGPacket Input(Fixture.Data.GetData(), Fixture.Data.Num());
	FShipObjectBaseline State;
	TestTrue(TEXT("complete baseline 6"), SWGShipBaselineParser::ParseBase6(Input, State));
	TestEqual(TEXT("compact movement ID"), State.ShipId, uint16(42));
	TestEqual(TEXT("flight values do not shift with populated maps"), State.Flight.MaxSpeed, 9.f);
	TestEqual(TEXT("component CRC"), State.ComponentCrcs.Items[0].Value, uint32(0x12345678));
	TestEqual(TEXT("trailing guild"), State.GuildId, uint32(30));
	FSWGPacket Short(Fixture.Data.GetData(), Fixture.Data.Num()-1);
	TestFalse(TEXT("truncated baseline rejected"), SWGShipBaselineParser::ParseBase6(Short, State));
	TestEqual(TEXT("failed parse keeps previous state"), State.GuildId, uint32(30));
	FSWGPacket Bad;
	Bad.WriteInt32(0x76); Bad.WriteInt32(-1); Bad.WriteUInt32(0);
	FSWGPacket BadInput(Bad.Data.GetData(), Bad.Data.Num());
	TestFalse(TEXT("negative collection count rejected"), SWGShipBaselineParser::ParseBase6(BadInput, State));
	FSWGPacket Owner;
	Owner.WriteInt32(0); Owner.WriteInt32(0);
	for (int32 Field = 2; Field <= 16; ++Field)
	{
		Owner.WriteInt32(1); Owner.WriteUInt32(Field); Owner.WriteByte(0); Owner.WriteUInt32(2);
		if (Field == 6) Owner.WriteUnicodeString(TEXT("Engine"));
		else if (Field == 7) Owner.WriteUInt64(123456);
		else if (Field >= 14) Owner.WriteUInt32(100);
		else Owner.WriteFloat(2.5f);
	}
	for (int32 Value = 1; Value <= 20; ++Value) Owner.WriteFloat(float(Value));
	Owner.WriteUInt64(99);
	Owner.SetReadMode();
	TestTrue(TEXT("owner baseline with populated maps"), SWGShipBaselineParser::ParseBase1(Owner, State));
	TestEqual(TEXT("Unicode component name"), State.ComponentNames.Items[0].Value, FString(TEXT("Engine")));
	TestEqual(TEXT("64-bit component creator"), State.ComponentCreators.Items[0].Value, uint64(123456));
	TestEqual(TEXT("last owner scalar after all maps"), State.OwnerValues[19], 20.f);
	TestEqual(TEXT("droid identity"), State.DroidObjectId, uint64(99));
	FSWGPacket Base4;
	for (int32 Value = 1; Value <= 4; ++Value) Base4.WriteFloat(float(Value));
	Base4.WriteInt32(1); Base4.WriteUInt32(4); Base4.WriteByte(0); Base4.WriteUInt32(2); Base4.WriteFloat(0.75f);
	Base4.SetReadMode();
	TestTrue(TEXT("owner energy baseline"), SWGShipBaselineParser::ParseBase4(Base4, State));
	TestEqual(TEXT("booster energy"), State.BoosterEnergy, 4.f);
	TestEqual(TEXT("refire efficiency"), State.RefireEfficiency.Items[0].Value, 0.75f);
	FSWGPacket Base3;
	Base3.WriteFloat(1); Base3.WriteAsciiString(TEXT("ship")); Base3.WriteInt32(0); Base3.WriteAsciiString(TEXT("yacht"));
	Base3.WriteUnicodeString(TEXT("My yacht")); Base3.WriteInt32(1); Base3.WriteAsciiString(TEXT(""));
	Base3.WriteInt32(0); Base3.WriteUInt32(0);
	for (int32 Field = 0; Field < 4; ++Field) Base3.WriteInt32(0);
	Base3.WriteByte(1);
	Base3.WriteFloat(0.5f); Base3.WriteFloat(100); Base3.WriteFloat(200); Base3.WriteUInt32(123);
	for (int32 Field = 0; Field < 5; ++Field) { Base3.WriteInt32(0); Base3.WriteUInt32(0); }
	Base3.WriteFloat(30); Base3.WriteFloat(40);
	Base3.SetReadMode();
	TestTrue(TEXT("chassis baseline"), SWGShipBaselineParser::ParseBase3(Base3, State));
	TestEqual(TEXT("chassis identity"), State.ChassisCrc, uint32(123));
	TestEqual(TEXT("name reused from tangible state"), State.Tangible.CustomName, FString(TEXT("My yacht")));
	TestEqual(TEXT("trailing shield after all component maps"), State.MaxRearShield, 40.f);
	FSWGPacket Delta;
	Delta.WriteUInt16(11); Delta.WriteFloat(150.f);
	Delta.WriteUInt16(15); Delta.WriteInt32(1); Delta.WriteUInt32(10);
	Delta.WriteByte(2); Delta.WriteUInt32(2); Delta.WriteUInt32(0x87654321);
	Delta.SetReadMode();
	TestTrue(TEXT("flight and component delta"), SWGShipBaselineParser::ApplyDelta(Delta, State, 6, 2));
	TestEqual(TEXT("effective maximum speed updates"), State.Flight.MaxSpeed, 150.f);
	TestEqual(TEXT("component map SET updates by slot"), State.ComponentCrcs.Items[0].Value, uint32(0x87654321));
	FSWGPacket Unknown;
	Unknown.WriteUInt16(11); Unknown.WriteFloat(300.f); Unknown.WriteUInt16(999);
	Unknown.SetReadMode();
	TestFalse(TEXT("unknown delta rejected atomically"), SWGShipBaselineParser::ApplyDelta(Unknown, State, 6, 2));
	TestEqual(TEXT("unknown trailing update cannot partially change flight"), State.Flight.MaxSpeed, 150.f);
	// Unmodified selected-yacht payloads from the CRC-validated retail capture; see RetailSpaceflightRuntimeEvidence.md.
	struct FCapturedBaseline { const TCHAR* Hex; bool (*Parse)(FSWGPacket&, FShipObjectBaseline&); };
	const FCapturedBaseline CapturedBaselines[] = {
		{ TEXT("0000000000000000070000000f00000000000000000000803f00010000000000803f00020000000000803f00040000000000803f00050000000000803f00060000000000803f00070000000000803f070000000f00000000000000000000803f00010000000000803f00020000000000803f00040000000000803f00050000000000803f00060000000000803f00070000000000803f0700000008000000000000000000000000000100000000e0c444000200000000c01f45000400000000000000000500000000000000000600000000007a4400070000000000fa43070000000800000000000000000000a54400010000000a33814500020000000080894400040000009ab95c4400050000009ab95c44000600000000007a44000700000000004844070000000800000000000000000f000000470065006e0065007200690063002000520065006100630074006f00720000010000001a00000049006e0063006f006d002000460075007300690061006c002000540068007200750073007400200045006e00670069006e00650000020000000e000000470065006e006500720069006300200053006800690065006c00640000040000000d000000470065006e0065007200690063002000410072006d006f00720000050000000d000000470065006e0065007200690063002000410072006d006f007200000600000011000000470065006e006500720069006300200043006100700061006300690074006f00720000070000000f000000470065006e006500720069006300200042006f006f007300740065007200000000000100000000000000010000000000000001000000000000000100000000000000010000000000000001000000000000000100000000000000010000000000000001000000000000000100000000401c465c8fc23f0000fa430000a0410000204100002041f3665f3ff3665f3f368d273f1a34853f1a34853f1a34853f33336b4200401c4600007a440000c8410000c8420000a04100007041000000000000000000000000"), SWGShipBaselineParser::ParseBase1 },
		{ TEXT("0000c8420a0073706163655f73686970000000001b00706c617965725f736f726f737575625f73706163655f79616368741e000000540065007200790020005c0023006600660066006600300030005b0053005700470045006d0075002d00410064006d0069006e005d005c0023002e000100000000000000000000000000000000000000000000000000e803000001cdcc8c3f0000bd440000bd443a7a37de0700000008000000000000000000008742000100000000a057430002000000000096420004000000cdcc24430005000000cdcc24430006000000000096420007000000000096420700000008000000000000000000008742000100000000a057430002000000000096420004000000cdcc24430005000000cdcc24430006000000000096420007000000000096420700000008000000000000000000000743000100000000a0d7430002000000000016430004000000cdcc24430005000000cdcc24430006000000000016430007000000000016430700000008000000000000000000000743000100000000a0d7430002000000000016430004000000cdcc24430005000000cdcc2443000600000000001643000700000000001643070000000f0000000000000000040000000001000000040000000002000000040000000004000000040000000005000000040000000006000000040000000007000000000000000000484300004843"), SWGShipBaselineParser::ParseBase3 },
		{ TEXT("b8201e46cdcc4c3f0000fa4300007a44070000000800000000000000000000803f00010000000000803f00020000000000803f00040000000000803f00050000000000803f00060000000000803f00070000000000803f"), SWGShipBaselineParser::ParseBase4 },
		{ TEXT("760000000000000001000000210a0000204100002041f3665f3ff3665f3f368d273f2a20553e2a20553e1a34853ff6283c420000000000000000000000000100000005000000070700000008000000000000000004fc2ed700010000003e98d3790002000000a260472c00040000009cd15fa800050000009cd15fa80006000000f2a1bbeb0007000000fa386d980000000000000800636976696c69616e000048430000484300000000"), SWGShipBaselineParser::ParseBase6 },
	};
	FShipObjectBaseline CapturedState;
	for (const FCapturedBaseline& Captured : CapturedBaselines)
	{
		const FStringView Hex(Captured.Hex);
		TArray<uint8> Payload;
		Payload.SetNumUninitialized(Hex.Len()/2);
		if (!TestEqual(TEXT("captured baseline hex decoded"), UE::String::HexToBytes(Hex, Payload.GetData()), Payload.Num())) return false;
		FSWGPacket CapturedPacket(Payload.GetData(), Payload.Num());
		if (!TestTrue(TEXT("complete captured yacht baseline accepted"), Captured.Parse(CapturedPacket, CapturedState))) return false;
		TestEqual(TEXT("captured baseline consumes all fields"), CapturedPacket.GetRemaining(), 0);
	}
	TestTrue(TEXT("captured ship has all required baselines"), CapturedState.bHasBase1 && CapturedState.bHasBase3 && CapturedState.bHasBase4 && CapturedState.bHasBase6);
	TestEqual(TEXT("captured yacht compact ID"), CapturedState.ShipId, uint16(2593));
	TestEqual(TEXT("captured yacht acceleration"), CapturedState.Flight.Acceleration, 10.f);
	TestEqual(TEXT("captured yacht deceleration"), CapturedState.Flight.Deceleration, 10.f);
	TestEqual(TEXT("captured yacht effective maximum speed"), CapturedState.Flight.MaxSpeed, 47.04f);
	TestTrue(TEXT("captured angular acceleration axes"), CapturedState.Flight.AngularAcceleration.Equals(FVector(.87266463,.87266463,.65449846), .000001));
	TestTrue(TEXT("captured angular rate axes"), CapturedState.Flight.AngularRates.Equals(FVector(.20813051,.20813051,1.04065251), .000001));
	TestEqual(TEXT("captured yacht installed components"), CapturedState.ComponentCrcs.Items.Num(), 7);
	TestEqual(TEXT("captured yacht trailing faction"), CapturedState.Faction, FString(TEXT("civilian")));
	return true;
}
#endif

