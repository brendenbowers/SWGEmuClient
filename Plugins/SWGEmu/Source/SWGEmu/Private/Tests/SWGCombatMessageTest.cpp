#include "Misc/AutomationTest.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/Object/CombatActionIn.h"
#include "Network/Messages/Zone/Object/CombatSpamIn.h"
#include "Network/Messages/Zone/Object/CommandQueueRemoveIn.h"

#if WITH_DEV_AUTOMATION_TESTS

// Payloads are assembled byte by byte rather than with FSWGPacket's own write
// helpers, so that a byte-order or length-prefix mistake shared by the reader
// and the writer can't cancel itself out and pass.
namespace
{
	struct FByteBuilder
	{
		TArray<uint8> Bytes;

		void U8(uint8 V) { Bytes.Add(V); }

		void U16(uint16 V)
		{
			Bytes.Add(V & 0xFF);
			Bytes.Add((V >> 8) & 0xFF);
		}

		void U32(uint32 V)
		{
			for (int32 i = 0; i < 4; ++i)
			{
				Bytes.Add((V >> (8 * i)) & 0xFF);
			}
		}

		void U64(uint64 V)
		{
			for (int32 i = 0; i < 8; ++i)
			{
				Bytes.Add((V >> (8 * i)) & 0xFF);
			}
		}

		void F32(float V)
		{
			uint32 Bits = 0;
			FMemory::Memcpy(&Bits, &V, 4);
			U32(Bits);
		}

		/** Core3 insertAscii: uint16 length then the raw bytes, unterminated. */
		void Ascii(const ANSICHAR* V)
		{
			const int32 Len = FCStringAnsi::Strlen(V);
			U16(static_cast<uint16>(Len));
			for (int32 i = 0; i < Len; ++i)
			{
				Bytes.Add(static_cast<uint8>(V[i]));
			}
		}

		/** Core3 insertUnicode: int32 character count then UCS-2 LE. */
		void Unicode(const ANSICHAR* V)
		{
			const int32 Len = FCStringAnsi::Strlen(V);
			U32(static_cast<uint32>(Len));
			for (int32 i = 0; i < Len; ++i)
			{
				U16(static_cast<uint16>(V[i]));
			}
		}

		FSWGPacket AsPacket() const { return FSWGPacket(Bytes.GetData(), Bytes.Num()); }
	};
}

// ── CommandQueueRemove (0x117) ────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGCommandQueueRemoveParseTest,
	"SWGEmu.Messages.CommandQueueRemove.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::SmokeFilter)

bool FSWGCommandQueueRemoveParseTest::RunTest(const FString& Parameters)
{
	// The success shape: QueueCommand::onComplete sends the cooldown with no error.
	{
		FByteBuilder B;
		B.U32(7);      // actionCount
		B.F32(4.25f);  // timer, seconds
		B.U32(0);      // error
		B.U32(0);      // errorDetail

		FSWGPacket Packet = B.AsPacket();
		FCommandQueueRemoveIn Reply;

		TestTrue(TEXT("parses"), Reply.Parse(Packet));
		TestEqual(TEXT("actionCount"), Reply.ActionCount, 7u);
		TestEqual(TEXT("timer"), Reply.Timer, 4.25f);
		TestTrue(TEXT("is success"), Reply.IsSuccess());
		TestTrue(TEXT("consumed the whole payload"), Packet.IsAtEnd());
	}

	// The out-of-range shape: QueueCommand::onFail's TOOFAR branch.
	{
		FByteBuilder B;
		B.U32(9);
		B.F32(0.f);
		B.U32(4); // TooFar
		B.U32(0);

		FSWGPacket Packet = B.AsPacket();
		FCommandQueueRemoveIn Reply;

		TestTrue(TEXT("parses"), Reply.Parse(Packet));
		TestFalse(TEXT("is not success"), Reply.IsSuccess());
		TestTrue(TEXT("reads as TooFar"), Reply.GetError() == ESWGCommandError::TooFar);
	}

	// The state-blocked shape carries the offending state bit in errorDetail.
	{
		FByteBuilder B;
		B.U32(3);
		B.F32(0.f);
		B.U32(5);  // InvalidState
		B.U32(12); // state bit index

		FSWGPacket Packet = B.AsPacket();
		FCommandQueueRemoveIn Reply;

		TestTrue(TEXT("parses"), Reply.Parse(Packet));
		TestTrue(TEXT("reads as InvalidState"), Reply.GetError() == ESWGCommandError::InvalidState);
		TestEqual(TEXT("errorDetail"), Reply.ErrorDetail, 12u);
	}

	return true;
}

// ── CombatAction (0xCC) ───────────────────────────────────────────────────────

namespace
{
	void BuildCombatActionHeader(FByteBuilder& B)
	{
		B.U32(0xDEADBEEF); // animationCrc
		B.U64(0x1122334455667788ull); // attackerId
		B.U64(0x99AABBCCDDEEFF00ull); // weaponId
		B.U8(0);    // attacker posture (Upright)
		B.U8(0xFF); // trails
		B.U8(0);    // unused
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGCombatActionParseTest,
	"SWGEmu.Messages.CombatAction.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::SmokeFilter)

bool FSWGCombatActionParseTest::RunTest(const FString& Parameters)
{
	// The shape CombatManager::broadcastCombatAction sends for a player swing:
	// one defender, 13-byte entry.
	{
		FByteBuilder B;
		BuildCombatActionHeader(B);
		B.U16(1);        // running index
		B.U64(0x4242ull); // defender id
		B.U8(0);         // defender posture
		B.U8(1);         // hit
		B.U8(0);         // clientEffectId
		B.U8(3);         // hitLocation
		B.U8(57);        // initialDamage

		FSWGPacket Packet = B.AsPacket();
		FCombatActionIn Action;

		TestTrue(TEXT("parses"), Action.Parse(Packet));
		TestEqual(TEXT("animationCrc"), Action.AnimationCrc, 0xDEADBEEFu);
		TestEqual(TEXT("attackerId"), Action.AttackerId, static_cast<int64>(0x1122334455667788ull));
		TestEqual(TEXT("weaponId"), Action.WeaponId, static_cast<int64>(0x99AABBCCDDEEFF00ull));
		TestEqual(TEXT("trails"), Action.Trails, static_cast<uint8>(0xFF));
		TestTrue(TEXT("detected the full entry form"), Action.HasHitDetail());

		if (TestEqual(TEXT("defender count"), Action.Defenders.Num(), 1))
		{
			TestEqual(TEXT("defender id"), Action.Defenders[0].ObjectId, static_cast<int64>(0x4242));
			TestEqual(TEXT("hit"), Action.Defenders[0].Hit, static_cast<uint8>(1));
			TestEqual(TEXT("hitLocation"), Action.Defenders[0].HitLocation, static_cast<uint8>(3));
			TestEqual(TEXT("initialDamage"), Action.Defenders[0].InitialDamage, static_cast<uint8>(57));
		}
		TestTrue(TEXT("consumed the whole payload"), Packet.IsAtEnd());
	}

	// The 11-byte entry the TangibleObject-attacker path emits, which carries
	// no hit location or damage.
	{
		FByteBuilder B;
		BuildCombatActionHeader(B);
		B.U16(1);
		B.U64(0x77ull);
		B.U8(0);
		B.U8(3); // dodge
		B.U8(0);

		FSWGPacket Packet = B.AsPacket();
		FCombatActionIn Action;

		TestTrue(TEXT("parses"), Action.Parse(Packet));
		TestFalse(TEXT("detected the short entry form"), Action.HasHitDetail());
		if (TestEqual(TEXT("defender count"), Action.Defenders.Num(), 1))
		{
			TestEqual(TEXT("hit"), Action.Defenders[0].Hit, static_cast<uint8>(3));
			TestEqual(TEXT("hitLocation defaults"), Action.Defenders[0].HitLocation, static_cast<uint8>(0));
		}
	}

	// Several defenders, each preceded by its own running index — the thing
	// that makes that uint16 an index rather than a count.
	{
		FByteBuilder B;
		BuildCombatActionHeader(B);
		for (int32 i = 0; i < 3; ++i)
		{
			B.U16(static_cast<uint16>(i + 1));
			B.U64(static_cast<uint64>(0x100 + i));
			B.U8(0);
			B.U8(1);
			B.U8(0);
			B.U8(1);
			B.U8(static_cast<uint8>(10 * i));
		}

		FSWGPacket Packet = B.AsPacket();
		FCombatActionIn Action;

		TestTrue(TEXT("parses"), Action.Parse(Packet));
		TestTrue(TEXT("detected the full entry form"), Action.HasHitDetail());
		if (TestEqual(TEXT("defender count"), Action.Defenders.Num(), 3))
		{
			TestEqual(TEXT("second defender id"), Action.Defenders[1].ObjectId, static_cast<int64>(0x101));
			TestEqual(TEXT("third defender damage"), Action.Defenders[2].InitialDamage, static_cast<uint8>(20));
		}
	}

	// The "attacker only" constructors stop after the header. Not an error.
	{
		FByteBuilder B;
		BuildCombatActionHeader(B);

		FSWGPacket Packet = B.AsPacket();
		FCombatActionIn Action;

		TestTrue(TEXT("parses"), Action.Parse(Packet));
		TestEqual(TEXT("no defenders"), Action.Defenders.Num(), 0);
	}

	return true;
}

// ── CombatSpam (0x134) ────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGCombatSpamParseTest,
	"SWGEmu.Messages.CombatSpam.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::SmokeFilter)

bool FSWGCombatSpamParseTest::RunTest(const FString& Parameters)
{
	// The stringfile shape, which is what an ordinary hit sends.
	{
		FByteBuilder B;
		B.U64(0xAAull); // attacker
		B.U64(0xBBull); // defender
		B.U64(0);       // item
		B.U32(137);     // damage
		B.Ascii("cbt_spam");
		B.U32(0); // padding
		B.Ascii("attack_hit");
		B.U8(1);        // auto colour
		B.Unicode("");  // no custom text

		FSWGPacket Packet = B.AsPacket();
		FCombatSpamIn Spam;

		TestTrue(TEXT("parses"), Spam.Parse(Packet));
		TestEqual(TEXT("attackerId"), Spam.AttackerId, static_cast<int64>(0xAA));
		TestEqual(TEXT("defenderId"), Spam.DefenderId, static_cast<int64>(0xBB));
		TestEqual(TEXT("damage"), Spam.Damage, 137);
		TestEqual(TEXT("stringId"), Spam.GetStringId(), FString(TEXT("@cbt_spam:attack_hit")));
		TestTrue(TEXT("no custom text"), Spam.CustomText.IsEmpty());
		TestTrue(TEXT("consumed the whole payload"), Packet.IsAtEnd());
	}

	// The custom-text shape: no ids, no stringfile, the finished line inline.
	{
		FByteBuilder B;
		B.U64(0);
		B.U64(0);
		B.U64(0);
		B.U32(0);
		B.Ascii("");
		B.U32(0);
		B.Ascii("");
		B.U8(11); // yellow
		B.Unicode("You feel much better.");

		FSWGPacket Packet = B.AsPacket();
		FCombatSpamIn Spam;

		TestTrue(TEXT("parses"), Spam.Parse(Packet));
		TestTrue(TEXT("no stringId"), Spam.GetStringId().IsEmpty());
		TestEqual(TEXT("custom text"), Spam.CustomText, FString(TEXT("You feel much better.")));
		TestEqual(TEXT("colour"), Spam.Color, static_cast<uint8>(11));
		TestTrue(TEXT("consumed the whole payload"), Packet.IsAtEnd());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
