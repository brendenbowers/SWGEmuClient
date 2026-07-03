#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

/**
 * Shared decode helpers for baseline/delta sub-object payloads.
 *
 * Core3's object state fields boil down to two list wire shapes:
 *
 *   "Vector/Set" style (DeltaVector<T>, AutoDeltaSet<T>, SkillList, AbilityList,
 *   SchematicList, WearablesDeltaVector, DeltaBitArray, DeltaSet<K,V> pairs):
 *     count(int32) updateCounter(uint32) [count x item]           ← no per-item tag
 *
 *   "Map" style (DeltaVectorMap<K,V> and its overrides: SkillModList,
 *   WaypointList, quest/experience maps):
 *     count(int32) updateCounter(uint32) [count x (cmd(uint8) item)]
 *     cmd is always ADD(0 or 1) in a fresh baseline dump — deltas are the
 *     only place remove(1)/set(2)/drop reuse this byte meaningfully.
 */
template<typename T>
struct TSWGBaselineList
{
	uint32 UpdateCounter = 0;
	TArray<T> Items;
};

/** Reads a StringId: ascii(file) + int32(filler, always 0) + ascii(stringId). */
struct FSWGStringId
{
	FString File;
	FString StringTableId;

	static FSWGStringId Read(FSWGPacket& Packet)
	{
		FSWGStringId Out;
		Out.File = Packet.ReadAsciiString();
		Packet.ReadInt32(); // filler
		Out.StringTableId = Packet.ReadAsciiString();
		return Out;
	}
};

template<typename T, typename FReadItem>
TSWGBaselineList<T> ReadBaselineVector(FSWGPacket& Packet, FReadItem ReadItem)
{
	TSWGBaselineList<T> Out;
	const int32 Count = Packet.ReadInt32();
	Out.UpdateCounter = Packet.ReadUInt32();
	Out.Items.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Out.Items.Add(ReadItem(Packet));
	}
	return Out;
}

template<typename T, typename FReadItem>
TSWGBaselineList<T> ReadBaselineMap(FSWGPacket& Packet, FReadItem ReadItem)
{
	TSWGBaselineList<T> Out;
	const int32 Count = Packet.ReadInt32();
	Out.UpdateCounter = Packet.ReadUInt32();
	Out.Items.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Packet.ReadByte(); // map command (ADD in a fresh baseline dump)
		Out.Items.Add(ReadItem(Packet));
	}
	return Out;
}
