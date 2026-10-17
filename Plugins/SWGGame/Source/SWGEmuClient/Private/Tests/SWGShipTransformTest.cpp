#include "Misc/AutomationTest.h"
#include "Network/Messages/Zone/ShipUpdateTransformMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Objects/World/SWGShip.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Common/SWGWorldScale.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGShipTransformTest, "SWG.Space.ShipTransform", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGShipTransformTest::RunTest(const FString& Parameters)
{
	// Fixed Core3 wire fixture: identity quaternion, X/Z/Y position, signed rates.
	uint8 Bytes[] = {8,0,0xb9,0x6f,2,0x76, 42,0, 127,0,0,0, 0xff,0x7f,0,0,1,0, 0xff,0x7f,0,0, 127,0,0x81, 4,3,2,1};
	FSWGMessage Reader(FSWGPacket(Bytes, UE_ARRAY_COUNT(Bytes)));
	auto Registered = FSWGMessageRegistry::Get().Create(uint32(ESWGMessageOp::ShipUpdateTransform), Reader);
	if (!TestTrue(TEXT("ship movement registered"), Registered.IsValid())) return false;
	auto& Ship = static_cast<FShipUpdateTransformMessage&>(*Registered);
	TestTrue(TEXT("complete fixture accepted"), Ship.bValid);
	TestEqual(TEXT("compact ID"), Ship.ShipId, uint16(42));
	TestTrue(TEXT("quaternion W first"), Ship.Rotation.Equals(FQuat::Identity));
	TestEqual(TEXT("position scale"), Ship.Position.X, 8000.0);
	TestEqual(TEXT("wire Z before Y"), Ship.Position.Y, 8000.0 / 32767.0);
	TestEqual(TEXT("zero Z"), Ship.Position.Z, 0.0);
	TestEqual(TEXT("packed zero direction means positive Z"), Ship.Velocity.Z, 512.0);
	TestTrue(TEXT("signed angular rates"), Ship.AngularRates.Equals(FVector(UE_PI/2, 0, -UE_PI/2), 0.000001));
	TestEqual(TEXT("sync stamp"), Ship.SyncStamp, uint32(0x01020304));
	// Real retail/Core3 session; source digest and capture limits are in RetailSpaceflightCapture.md.
	const uint8 RetailCruise[][29] = {
		{0x08,0,0xb9,0x6f,0x02,0x76,0,0,0x7e,0,0,0,0x61,0x6c,0x30,0xb9,0x78,0xfc,0xe1,0x05,0,0xc0,0,0,0,0xe5,0x37,0x05,0x02},
		{0x08,0,0xb9,0x6f,0x02,0x76,0x21,0x0a,0x7e,0,0,0,0x60,0x6c,0x30,0xb9,0xa2,0xfc,0xe1,0x05,0,0xc0,0,0,0,0xe6,0x37,0x05,0x02}
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(RetailCruise); ++Index)
	{
		FSWGMessage CaptureReader(FSWGPacket(RetailCruise[Index], UE_ARRAY_COUNT(RetailCruise[Index])));
		FShipUpdateTransformMessage Capture(0, CaptureReader);
		TestTrue(TEXT("captured cruise packet accepted completely"), Capture.bValid && CaptureReader.GetRemaining() == 0);
		TestEqual(TEXT("retail sender zero ID and server assigned ID"), Capture.ShipId, uint16(Index ? 2593 : 0));
		TestTrue(TEXT("quantized retail quaternion normalized"), Capture.Rotation.Equals(FQuat::Identity));
		const FVector PackedPosition = Index ? FVector(27744,-862,-18128) : FVector(27745,-904,-18128);
		TestTrue(TEXT("captured position axes and scale"), Capture.Position.Equals(PackedPosition * (8000.0 / 32767.0), .000001));
		TestTrue(TEXT("captured signed direction points along native Z"), Capture.Velocity.Equals(FVector(0,0,1505.0 * 512.0 / 32767.0), .000001));
		TestTrue(TEXT("captured cruise has zero angular rates"), Capture.AngularRates.IsZero());
		TestEqual(TEXT("server echo retains millisecond clock"), Capture.SyncStamp, uint32(33896421 + Index));
	}
	FSWGMessage ShortReader(FSWGPacket(Bytes, UE_ARRAY_COUNT(Bytes)-1));
	FShipUpdateTransformMessage Short(0, ShortReader);
	TestFalse(TEXT("truncation rejected"), Short.bValid);
	Bytes[8] = 0;
	FSWGMessage ZeroReader(FSWGPacket(Bytes, UE_ARRAY_COUNT(Bytes)));
	FShipUpdateTransformMessage Zero(0, ZeroReader);
	TestFalse(TEXT("zero quaternion rejected"), Zero.bValid);
	FShipUpdateTransformMessage Sent;
	Sent.ShipId = 42; Sent.Position = FVector(-500, 250, -125);
	Sent.Velocity = FVector(-200, 100, -50); Sent.AngularRates = FVector(-0.5, 0.25, -1);
	Sent.Rotation = FQuat(FVector::UpVector, 0.7); Sent.SyncStamp = 12345;
	FSWGPacket Encoded;
	TestTrue(TEXT("flight sample encoded"), Sent.Serialize(Encoded));
	FSWGMessage RoundReader(FSWGPacket(Encoded.Data.GetData(), Encoded.Data.Num()));
	FShipUpdateTransformMessage Received(0, RoundReader);
	TestTrue(TEXT("flight sample decoded"), Received.bValid);
	TestTrue(TEXT("signed position within quantization"), Received.Position.Equals(Sent.Position, 0.25));
	TestEqual(TEXT("Core3 direction low byte"), Encoded.Data[20], uint8(0xd1));
	TestEqual(TEXT("Core3 direction signs and high byte"), Encoded.Data[21], uint8(0xa8));
	TestTrue(TEXT("packed speed within quantization"), FMath::Abs(Received.Velocity.Size() - Sent.Velocity.Size()) < 0.02);
	TestTrue(TEXT("biased direction quantization retains heading"), FVector::DotProduct(Received.Velocity.GetSafeNormal(), Sent.Velocity.GetSafeNormal()) > 0.997);
	TestTrue(TEXT("angular rates within quantization"), Received.AngularRates.Equals(Sent.AngularRates, 0.013));
	TestTrue(TEXT("quaternion within quantization"), Received.Rotation.Equals(Sent.Rotation, 0.02));
	TestEqual(TEXT("stamp round trip"), Received.SyncStamp, Sent.SyncStamp);
	Sent.Position.X = 8001;
	TestFalse(TEXT("out-of-zone transform cannot be sent"), Sent.Serialize(Encoded));
	struct FTransformGolden { float Angle; FVector Position; uint8 Bytes[10]; };
	const FTransformGolden RetailTransforms[] = {
		{0,FVector::ZeroVector,{0x7f,0,0,0,0,0,0,0,0,0}},
		{1.5707963267948966f,FVector(8000,.5,-8000),{0x59,0x18,0x30,0x48,0xfe,0x7f,0x02,0x80,0x02,0}},
		{3.141592653589793f,FVector(-123.456f,-6789.75f,345.125f),{0,0x21,0x43,0x65,0x07,0xfe,0x85,0x05,0x5f,0x93}},
		{.7f,FVector(7999,1,-7999),{0x77,0x0b,0x17,0x22,0xfa,0x7f,0x06,0x80,0x04,0}}
	};
	for (const FTransformGolden& Sample : RetailTransforms)
	{
		FShipUpdateTransformMessage Golden;
		Golden.Rotation = FQuat(FVector(1,2,3).GetSafeNormal(), Sample.Angle);
		Golden.Position = Sample.Position;
		if (!TestTrue(TEXT("retail transform sample encodes"), Golden.Serialize(Encoded))) return false;
		for (int32 Byte = 0; Byte < 10; ++Byte)
			TestEqual(FString::Printf(TEXT("retail transform angle %.3f byte %d"), Sample.Angle, Byte), Encoded.Data[8+Byte], Sample.Bytes[Byte]);
	}
	// Retail 0xad67e0/0xad6c40 outputs; see RetailFlightMathEvidence.txt for harness limits.
	struct FVelocityGolden { FVector Velocity; uint16 Speed; uint16 Direction; FVector Decoded; };
	const FVelocityGolden RetailSamples[] = {
		{FVector(-200,100,-50),0x3947,0xa8d1,FVector(-198.319443,96.3265839,-62.328968)},
		{FVector(100,0,0),0x18ff,0x0f80,FVector(99.9744186,0,1.61249065)},
		{FVector(0,-100,0),0x18ff,0x403e,FVector(0,-99.9744186,1.61249065)},
		{FVector(0,0,100),0x18ff,0x0000,FVector(0,0,99.9874268)},
		{FVector(0,0,-100),0x18ff,0x2000,FVector(0,0,-99.9874268)},
		{FVector(300,400,0),0x7cff,0x06a3,FVector(297.848572,400.950012,22.9114285)},
		{FVector(600,0,0),0x7fff,0x0f80,FVector(511.933411,0,8.25699043)},
		{FVector(-1,-1,-1),0x006e,0xe514,FVector(-.942960441,-.942960441,-1.08440447)},
		{FVector(.001f,.002f,.003f),0x0000,0x0294,FVector::ZeroVector},
		{FVector::ZeroVector,0x0000,0x0000,FVector::ZeroVector}
	};
	for (const FVelocityGolden& Sample : RetailSamples)
	{
		FShipUpdateTransformMessage Golden;
		Golden.Velocity = Sample.Velocity;
		if (!TestTrue(TEXT("retail velocity sample encodes"), Golden.Serialize(Encoded))) return false;
		TestEqual(TEXT("retail packed speed bytes"), uint16(Encoded.Data[18] | (uint16(Encoded.Data[19]) << 8)), Sample.Speed);
		TestEqual(TEXT("retail packed direction bytes"), uint16(Encoded.Data[20] | (uint16(Encoded.Data[21]) << 8)), Sample.Direction);
		FSWGMessage GoldenReader(FSWGPacket(Encoded.Data.GetData(), Encoded.Data.Num()));
		FShipUpdateTransformMessage Decoded(0, GoldenReader);
		TestTrue(TEXT("retail velocity sample decodes"), Decoded.bValid);
		TestTrue(TEXT("retail unpacked velocity"), Decoded.Velocity.Equals(Sample.Decoded, .0001));
	}
	// The angular capture executes a bounded constructor block, not the full retail message constructor.
	struct FAngularGolden { float Rate; uint8 Byte; double Decoded; };
	const FAngularGolden RetailRates[] = {
		{-2,0x81,-1.57079637}, {-1.5707963267948966f,0x81,-1.57079637},
		{-.5f,0xd8,-.494739026}, {-.01f,0,0}, {0,0,0}, {.01f,0,0},
		{.25f,0x14,.247369513}, {.5f,0x28,.494739026},
		{1.5707963267948966f,0x7f,1.57079637}, {2,0x7f,1.57079637}
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(RetailRates); ++Index)
	{
		FShipUpdateTransformMessage Golden;
		FVector ExpectedRates;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FAngularGolden& Sample = RetailRates[(Index+Axis)%UE_ARRAY_COUNT(RetailRates)];
			Golden.AngularRates[Axis] = Sample.Rate;
			ExpectedRates[Axis] = Sample.Decoded;
		}
		if (!TestTrue(TEXT("retail angular sample encodes"), Golden.Serialize(Encoded))) return false;
		for (int32 Axis = 0; Axis < 3; ++Axis)
			TestEqual(TEXT("retail signed angular byte"), Encoded.Data[22+Axis], RetailRates[(Index+Axis)%UE_ARRAY_COUNT(RetailRates)].Byte);
		FSWGMessage GoldenReader(FSWGPacket(Encoded.Data.GetData(), Encoded.Data.Num()));
		FShipUpdateTransformMessage Decoded(0, GoldenReader);
		TestTrue(TEXT("retail angular sample decodes"), Decoded.bValid);
		TestTrue(TEXT("retail unpacked angular rates"), Decoded.AngularRates.Equals(ExpectedRates, .000001));
	}
	FSWGPacket Collision;
	Collision.WriteUInt16(8); Collision.WriteUInt32(uint32(ESWGMessageOp::ShipUpdateTransformCollision)); Collision.WriteUInt64(987654321);
	const FQuat CollisionRotation = FQuat(FVector(1,2,3).GetSafeNormal(), .7);
	for (double Component : {CollisionRotation.X, CollisionRotation.Z, CollisionRotation.Y, CollisionRotation.W}) Collision.WriteFloat(Component);
	for (double Component : {100., 200., 300.}) Collision.WriteFloat(Component * 4.0958748);
	for (int32 Axis = 0; Axis < 3; ++Axis) Collision.WriteFloat(0);
	Collision.WriteUInt32(27);
	FSWGMessage CollisionReader(FSWGPacket(Collision.Data.GetData(), Collision.Data.Num()));
	auto RegisteredCollision = FSWGMessageRegistry::Get().Create(uint32(ESWGMessageOp::ShipUpdateTransformCollision), CollisionReader);
	if (!TestTrue(TEXT("collision opcode registered"), RegisteredCollision.IsValid())) return false;
	const auto& Hit = static_cast<const FShipUpdateTransformCollisionMessage&>(*RegisteredCollision);
	TestTrue(TEXT("Core3 collision fixture valid"), Hit.bValid);
	TestEqual(TEXT("collision uses full object ID"), Hit.ObjectId, uint64(987654321));
	TestTrue(TEXT("collision quaternion X Z Y restored"), Hit.Rotation.Equals(CollisionRotation, .0001));
	TestTrue(TEXT("collision position multiplier restored"), Hit.Position.Equals(FVector(100,200,300), .001));
	TestEqual(TEXT("collision counter is not a sync clock"), Hit.MovementCounter, uint32(27));
	FSWGMessage ShortCollisionReader(FSWGPacket(Collision.Data.GetData(), Collision.Data.Num()-1));
	FShipUpdateTransformCollisionMessage ShortCollision(0, ShortCollisionReader);
	TestFalse(TEXT("truncated collision rejected"), ShortCollision.bValid);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGShipFlightTest, "SWG.Space.ShipFlight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGShipFlightTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ASWGShip* Slow = World->SpawnActor<ASWGShip>();
	ASWGShip* Fast = World->SpawnActor<ASWGShip>();
	if (!Slow || !Fast) return false;
	Slow->ShipState.Flight.Acceleration = 20;
	Slow->ShipState.Flight.Deceleration = 40;
	Slow->ShipState.Flight.MaxSpeed = 100;
	Slow->ShipState.Flight.AngularAcceleration = FVector(2);
	Slow->ShipState.Flight.AngularRates = FVector(1);
	Fast->ShipState = Slow->ShipState;
	Slow->IntegrateFlight(0.1f, 1, FVector::ZeroVector);
	TestTrue(TEXT("no flight before baseline"), Slow->SpacePose.Equals(FTransform::Identity));
	Slow->bHasFlightBaseline = Fast->bHasFlightBaseline = true;
	for (int32 Frame = 0; Frame < 60; ++Frame) Slow->IntegrateFlight(1.f/30.f, 1, FVector::ZeroVector);
	for (int32 Frame = 0; Frame < 288; ++Frame) Fast->IntegrateFlight(1.f/144.f, 1, FVector::ZeroVector);
	TestTrue(TEXT("server acceleration in metres per second squared"), FMath::IsNearlyEqual(Slow->SpaceVelocity.Size(), 4000., .1));
	// Retail's per-frame updated-velocity Euler sum is a*dt^2*n*(n+1)/2.
	TestTrue(TEXT("30fps displacement matches retail frame update order"), Slow->SpacePose.GetLocation().Equals(FVector(4066.6667,0,0), .1));
	TestTrue(TEXT("144fps displacement matches retail frame update order"), Fast->SpacePose.GetLocation().Equals(FVector(4013.8889,0,0), .1));
	TestTrue(TEXT("frame-rate variation remains within the Euler acceleration error"), FVector::Dist(Slow->SpacePose.GetLocation(), Fast->SpacePose.GetLocation()) < 54);
	for (int32 Frame = 0; Frame < 30; ++Frame) Slow->IntegrateFlight(1.f/30.f, 0, FVector::ZeroVector);
	TestTrue(TEXT("server deceleration stops ship"), Slow->SpaceVelocity.IsNearlyZero(.1));
	const FVector Stopped = Slow->SpacePose.GetLocation();
	for (int32 Frame = 0; Frame < 30; ++Frame) Slow->IntegrateFlight(1.f/30.f, 0, FVector(1,1,1));
	TestTrue(TEXT("stationary pitch yaw roll all integrate"), !Slow->SpacePose.GetRotation().Equals(FQuat::Identity, .01));
	TestTrue(TEXT("angular acceleration respects each server rate"), Slow->FlightAngularRates.Equals(FVector(1), .01));
	TestTrue(TEXT("stationary rotation does not translate"), Slow->SpacePose.GetLocation().Equals(Stopped, .01));
	TestTrue(TEXT("POB actor remains fixed while space pose moves"), Slow->GetActorTransform().Equals(FTransform::Identity));
	const FTransform Previous = Slow->SpacePose;
	Slow->IntegrateFlight(0.1f, 1, FVector(NAN,0,0));
	TestTrue(TEXT("nonfinite input rejected"), Slow->SpacePose.Equals(Previous));
	Slow->SpacePose = Fast->SpacePose = FTransform(FRotator(0,90,0), FVector::ZeroVector);
	Slow->SpaceVelocity = Fast->SpaceVelocity = FVector(10000,0,0);
	Slow->FlightSpeed = Fast->FlightSpeed = 10000;
	Slow->FlightAngularRates = Fast->FlightAngularRates = FVector::ZeroVector;
	Slow->ShipState.Slip = 1.9f;
	Slow->IntegrateFlight(1.f/120.f, 1, FVector::ZeroVector);
	Fast->IntegrateFlight(1.f/120.f, 1, FVector::ZeroVector);
	TestTrue(TEXT("retail slip retains momentum while turning toward thrust"), Slow->SpaceVelocity.X > 0 && Slow->SpaceVelocity.Y > 0);
	TestTrue(TEXT("retail slip caps velocity to scalar engine speed"), Slow->SpaceVelocity.Size() <= 10000.001);
	TestTrue(TEXT("zero slip immediately follows new heading"), Fast->SpaceVelocity.Equals(FVector(0,10000,0), .01));
	Slow->SpacePose = FTransform(FRotator(0,90,0), FVector::ZeroVector);
	Slow->SpaceVelocity = FVector(10000,0,0); Slow->FlightSpeed = 10000;
	Slow->IntegrateFlight(.1f, 1, FVector::ZeroVector);
	const FVector RetailSlipVelocity = FVector(10000,1900,0).GetSafeNormal()*10000;
	TestTrue(TEXT("retail frame-sized slip impulse is not substepped"), Slow->SpaceVelocity.Equals(RetailSlipVelocity,.01));
	TestTrue(TEXT("retail position uses the newly capped slip velocity"), Slow->SpacePose.GetLocation().Equals(RetailSlipVelocity*.1,.01));
	Fast->SpacePose = FTransform::Identity; Fast->SpaceVelocity = FVector::ZeroVector; Fast->FlightSpeed = 0;
	Fast->IntegrateFlight(.1f, 1, FVector::ZeroVector);
	TestTrue(TEXT("first acceleration frame uses updated speed for translation"), Fast->SpacePose.GetLocation().Equals(FVector(20,0,0), .01));
	// Captured retail11e55f0/11e5640 math, native axes converted to UE; see RetailFlightMathEvidence.txt.
	Fast->SpacePose = FTransform::Identity; Fast->SpaceVelocity = FVector::ZeroVector; Fast->FlightSpeed = 0;
	Fast->FlightAngularRates = FVector::ZeroVector;
	Fast->IntegrateFlight(.1f, 1, FVector(.4,.7,.3));
	TestTrue(TEXT("retail mixed-axis forward vector"), Fast->SpacePose.GetRotation().GetForwardVector().Equals(FVector(.999600053,.0199946687,.0199986678), 1.e-6));
	TestTrue(TEXT("retail mixed-axis right vector"), Fast->SpacePose.GetRotation().GetRightVector().Equals(FVector(-.0203945357,.999592066,.0199946687), 1.e-6));
	TestTrue(TEXT("retail mixed-axis up vector"), Fast->SpacePose.GetRotation().GetUpVector().Equals(FVector(-.0195907224,-.0203945357,.999600053), 1.e-6));
	TestTrue(TEXT("retail mixed-axis velocity in centimetres"), Fast->SpaceVelocity.Equals(FVector(199.920011,3.99893373,3.99973355), .001));
	TestTrue(TEXT("retail mixed-axis translation in centimetres"), Fast->SpacePose.GetLocation().Equals(FVector(19.9920014,.399893383,.399973383), .001));
	Fast->ShipState.Flight.MaxSpeed = 200;
	Fast->ShipState.Slip = 1.9f;
	for (int32 Sign : {-1, 1})
	{
		Fast->SpacePose = FTransform(FQuat::Identity, FVector(Sign*7999, Sign*7999, -Sign*7999)*SWGWorldScale);
		Fast->SpaceVelocity = FVector(Sign*100, Sign*100, -Sign*100)*SWGWorldScale;
		Fast->FlightSpeed = 200*SWGWorldScale;
		Fast->FlightAngularRates = FVector::ZeroVector;
		Fast->IntegrateFlight(.1f, 1, FVector::ZeroVector);
		TestTrue(FString::Printf(TEXT("retail boundary clamps all axes, sign %d"), Sign),
			Fast->SpacePose.GetLocation().Equals(FVector(Sign*8000, Sign*8000, -Sign*8000)*SWGWorldScale, .01));
		const FVector RetailVelocity = Sign < 0 ? FVector(-62,-100,100) : FVector(138,100,-100);
		TestTrue(FString::Printf(TEXT("retail boundary retains velocity, sign %d"), Sign),
			Fast->SpaceVelocity.Equals(RetailVelocity*SWGWorldScale, .01));
	}
	return true;
}
#endif
