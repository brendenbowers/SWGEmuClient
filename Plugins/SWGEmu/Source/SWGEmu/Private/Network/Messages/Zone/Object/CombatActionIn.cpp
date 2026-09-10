#include "Network/Messages/Zone/Object/CombatActionIn.h"
#include "Network/SWGPacket.h"

namespace
{
	// A defender entry is its 1-based index plus the entry body.
	constexpr int32 IndexSize = 2;
	constexpr int32 ShortEntryBody = 11; // objectId + posture + hit + clientEffectId
	constexpr int32 FullEntryBody  = 13; // ... + hitLocation + initialDamage
}

bool FCombatActionIn::Parse(FSWGPacket& Packet)
{
	AnimationCrc    = Packet.ReadUInt32();
	AttackerId      = Packet.ReadInt64();
	WeaponId        = Packet.ReadInt64();
	AttackerPosture = Packet.ReadByte();
	Trails          = Packet.ReadByte();
	Packet.ReadByte(); // unused, always zero

	if (Packet.IsError())
	{
		return false;
	}

	const int32 Remaining = Packet.GetRemaining();
	if (Remaining <= 0)
	{
		// The two "attacker only" constructors emit a zero defender count and
		// stop. Nothing to read, and not an error.
		return true;
	}

	// Infer the entry form from the length. N full entries occupy
	// N * (2 + 13) bytes and N short ones N * (2 + 11); only a lone defender
	// is ever written in the short form, so the two can never collide.
	const bool bFull = (Remaining % (IndexSize + FullEntryBody)) == 0;
	bHasHitDetail = bFull;

	const int32 EntrySize = IndexSize + (bFull ? FullEntryBody : ShortEntryBody);
	if (Remaining % EntrySize != 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FCombatActionIn: %d trailing byte(s) match no defender entry size — dropping the defender list"), Remaining);
		return false;
	}

	const int32 Count = Remaining / EntrySize;
	Defenders.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		Packet.ReadUInt16(); // running 1-based index, not a count

		FSWGCombatDefender& Defender = Defenders.AddDefaulted_GetRef();
		Defender.ObjectId       = Packet.ReadInt64();
		Defender.Posture        = Packet.ReadByte();
		Defender.Hit            = Packet.ReadByte();
		Defender.ClientEffectId = Packet.ReadByte();

		if (bFull)
		{
			Defender.HitLocation   = Packet.ReadByte();
			Defender.InitialDamage = Packet.ReadByte();
		}
	}

	return !Packet.IsError();
}
