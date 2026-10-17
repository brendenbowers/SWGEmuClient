#include "Misc/AutomationTest.h"

#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/Zone/EffectMessages.h"
#include "Network/SWGPacket.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Little-endian bytes written independently of FSWGPacket's own helpers, mirroring Core3's BaseMessage inserts.
	struct FEffectByteBuilder
	{
		TArray<uint8> Bytes;

		void U16(uint16 Value)
		{
			Bytes.Add(Value & 0xFF);
			Bytes.Add((Value >> 8) & 0xFF);
		}

		void U32(uint32 Value)
		{
			for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
			{
				Bytes.Add((Value >> (8 * ByteIndex)) & 0xFF);
			}
		}

		void U64(uint64 Value)
		{
			for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
			{
				Bytes.Add((Value >> (8 * ByteIndex)) & 0xFF);
			}
		}

		void F32(float Value)
		{
			uint32 Bits = 0;
			FMemory::Memcpy(&Bits, &Value, 4);
			U32(Bits);
		}

		void Ascii(const ANSICHAR* Value)
		{
			const int32 Length = FCStringAnsi::Strlen(Value);
			U16(static_cast<uint16>(Length));
			for (int32 CharIndex = 0; CharIndex < Length; ++CharIndex)
			{
				Bytes.Add(static_cast<uint8>(Value[CharIndex]));
			}
		}

		FSWGPacket AsPacket() const { return FSWGPacket(Bytes.GetData(), Bytes.Num()); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGEffectMessagesParseTest,
	"SWGEmu.Messages.Effects.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSWGEffectMessagesParseTest::RunTest(const FString& Parameters)
{
	{
		FEffectByteBuilder Builder;
		Builder.U16(5);
		Builder.U32(0x4F5E09B6u);
		Builder.Ascii("clienteffect/level_granted.cef");
		Builder.F32(0.1f); Builder.F32(0.2f); Builder.F32(0.3f); Builder.F32(0.9f);
		Builder.F32(10.f); Builder.F32(20.f); Builder.F32(30.f);
		Builder.U64(0x1122334455667788ull);
		FSWGMessage Reader(Builder.AsPacket());
		TSharedPtr<FSWGNetMessage> Message = FSWGMessageRegistry::Get().Create(Reader.Opcode, Reader);
		if (!TestTrue(TEXT("transform opcode registered"), Message.IsValid())) return false;
		const auto& Effect = *static_cast<const FPlayClientEffectObjectTransformMessage*>(Message.Get());
		TestTrue(TEXT("transform packet valid"), Effect.bValid);
		TestEqual(TEXT("transform cef"), Effect.EffectFile, FString(TEXT("clienteffect/level_granted.cef")));
		TestEqual(TEXT("quaternion X first"), Effect.RotationX, 0.1f);
		TestEqual(TEXT("quaternion W fourth"), Effect.RotationW, 0.9f);
		TestEqual(TEXT("native X"), Effect.X, 10.f);
		TestEqual(TEXT("native up Y"), Effect.Y, 20.f);
		TestEqual(TEXT("native forward Z"), Effect.Z, 30.f);
		TestEqual(TEXT("transform object id last"), Effect.ObjectId, 0x1122334455667788ull);
		TestTrue(TEXT("transform payload consumed"), Reader.IsAtEnd());
		Builder.Bytes.Pop();
		FSWGMessage TruncatedReader(Builder.AsPacket());
		FPlayClientEffectObjectTransformMessage Truncated(TruncatedReader.Opcode, TruncatedReader);
		TestFalse(TEXT("truncated transform rejected"), Truncated.bValid);
		Builder.Bytes.Add(0);
		const int32 QuaternionStart = 6 + 2 + FCStringAnsi::Strlen("clienteffect/level_granted.cef");
		FMemory::Memzero(Builder.Bytes.GetData() + QuaternionStart, 16);
		FSWGMessage InvalidReader(Builder.AsPacket());
		FPlayClientEffectObjectTransformMessage Invalid(InvalidReader.Opcode, InvalidReader);
		TestFalse(TEXT("zero quaternion rejected"), Invalid.bValid);
	}
	{
		FEffectByteBuilder Builder;
		Builder.U16(5);
		Builder.U32(0x04270D8Au);
		Builder.Ascii("sound/music_badge_sting.snd");
		Builder.U64(0);
		Builder.U32(1);
		Builder.Bytes.Add(0);

		FSWGPacket Packet = Builder.AsPacket();
		FSWGMessage Reader(Packet);
		TestEqual(TEXT("music opcode"), Reader.Opcode, static_cast<uint32>(ESWGMessageOp::PlayMusicMessage));
		FPlayMusicMessage Music(Reader.Opcode, Reader);
		TestEqual(TEXT("music file"), Music.SoundFile, FString(TEXT("sound/music_badge_sting.snd")));
		TestEqual(TEXT("music target"), Music.Target, 0ull);
		TestEqual(TEXT("music unknown int"), Music.UnknownInt, 1u);
		TestEqual(TEXT("music unknown bool"), static_cast<int32>(Music.UnknownBool), 0);
	}

	{
		FEffectByteBuilder Builder;
		Builder.U16(5);
		Builder.U32(0x8855434Au);
		Builder.Ascii("clienteffect/level_granted.cef");
		Builder.Ascii("head");
		Builder.U64(0x1122334455667788ull);

		FSWGPacket Packet = Builder.AsPacket();
		FSWGMessage Reader(Packet);
		TestEqual(TEXT("object opcode"), Reader.Opcode, static_cast<uint32>(ESWGMessageOp::PlayClientEffectObjectMessage));
		FPlayClientEffectObjectMessage Effect(Reader.Opcode, Reader);
		TestEqual(TEXT("object cef"), Effect.EffectFile, FString(TEXT("clienteffect/level_granted.cef")));
		TestEqual(TEXT("object hardpoint"), Effect.Hardpoint, FString(TEXT("head")));
		TestEqual(TEXT("object id"), Effect.ObjectId, 0x1122334455667788ull);
	}

	{
		FEffectByteBuilder Builder;
		Builder.U16(9);
		Builder.U32(0x02949E74u);
		Builder.Ascii("clienteffect/survey_pulse.cef");
		Builder.Ascii("tatooine");
		Builder.F32(10.5f);
		Builder.F32(2.f);
		Builder.F32(-30.25f);
		Builder.U64(77);
		Builder.U32(0);
		Builder.Ascii("");

		FSWGPacket Packet = Builder.AsPacket();
		FSWGMessage Reader(Packet);
		TestEqual(TEXT("loc opcode"), Reader.Opcode, static_cast<uint32>(ESWGMessageOp::PlayClientEffectLoc));
		FPlayClientEffectLocMessage Effect(Reader.Opcode, Reader);
		TestEqual(TEXT("loc cef"), Effect.EffectFile, FString(TEXT("clienteffect/survey_pulse.cef")));
		TestEqual(TEXT("loc planet"), Effect.Planet, FString(TEXT("tatooine")));
		TestEqual(TEXT("loc x"), Effect.X, 10.5f);
		TestEqual(TEXT("loc z"), Effect.Z, 2.f);
		TestEqual(TEXT("loc y"), Effect.Y, -30.25f);
		TestEqual(TEXT("loc cell"), Effect.CellId, 77ull);
		TestEqual(TEXT("loc label"), Effect.Label, FString());
	}

	{
		FEffectByteBuilder Builder;
		Builder.U16(5);
		Builder.U32(0xAF83C3F2u);
		Builder.Ascii("cheer");
		Builder.Ascii("");
		Builder.U64(42);

		FSWGPacket Packet = Builder.AsPacket();
		FSWGMessage Reader(Packet);
		TestEqual(TEXT("event opcode"), Reader.Opcode, static_cast<uint32>(ESWGMessageOp::PlayClientEventObjectMessage));
		FPlayClientEventObjectMessage Event(Reader.Opcode, Reader);
		TestEqual(TEXT("event name"), Event.EventName, FString(TEXT("cheer")));
		TestEqual(TEXT("event id"), Event.ObjectId, 42ull);
	}

	return true;
}

#endif
