#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"

/**
 * Shared decode helpers for the list-change payloads inside a DeltasMessage.
 *
 * Where a baseline dumps a whole collection, a delta sends only what changed to
 * it, as a run of operations:
 *
 *   count(int32) updateCounter(uint32) [count x (op(uint8) operands...)]
 *
 * The operation bytes are NOT shared across list types — each server-side
 * container writes its own table, so a field's container decides which reader
 * below is correct for it (all verified against Core3, see each reader):
 *
 *   DeltaVector<E>       — position-addressed, ops carry an int16 index
 *   DeltaSet<K,V>        — value-addressed, ops carry the item
 *   DeltaVectorMap<K,V>  — value-addressed, ops carry key+value, and its
 *                          add/drop bytes are the reverse of DeltaSet's.
 */
enum class ESWGListChangeOperation : uint8
{
	Add,
	Remove,
	Set,
	ClearAll,
};

/**
 * One operation from a delta's list-change run.
 *
 * Which members carry anything depends on Operation and on which container the
 * change came from:
 *   Add      — Value, plus Index for DeltaVector
 *   Remove   — Index for DeltaVector, Value for DeltaSet/DeltaVectorMap
 *   Set      — Index and Value for DeltaVector, Value for DeltaVectorMap
 *   ClearAll — nothing
 */
template<typename T>
struct TSWGListChange
{
	ESWGListChangeOperation Operation = ESWGListChangeOperation::Add;
	int32 Index = INDEX_NONE;
	T Value{};
};

template<typename T>
struct TSWGListChanges
{
	uint32 UpdateCounter = 0;
	TArray<TSWGListChange<T>> Changes;
};

/**
 * Walks a delta payload's updates, handing each field index to ApplyUpdate to
 * read its operand. Stops early if ApplyUpdate returns false: operands are only
 * self-delimiting once the field is known, so an unrecognised index makes the
 * rest of the payload unreadable.
 */
template<typename FApplyUpdate>
void ReadDeltaUpdates(FSWGPacket& Packet, uint16 UpdateCount, FApplyUpdate ApplyUpdate)
{
	for (uint16 i = 0; i < UpdateCount; ++i)
	{
		const uint16 Index = Packet.ReadUInt16();
		if (!ApplyUpdate(Packet, Index))
		{
			UE_LOG(LogTemp, Warning, TEXT("ReadDeltaUpdates: unknown update index 0x%02X — stopping"), Index);
			return;
		}
	}
}

/** Reads the count/updateCounter header, then defers each operation to ReadChange. */
template<typename T, typename FReadChange>
TSWGListChanges<T> ReadListChanges(FSWGPacket& Packet, FReadChange ReadChange)
{
	TSWGListChanges<T> Out;
	const int32 Count = Packet.ReadInt32();
	Out.UpdateCounter = Packet.ReadUInt32();
	Out.Changes.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Out.Changes.Add(ReadChange(Packet));
	}
	return Out;
}

/**
 * Position-addressed changes, for fields the server keeps in a DeltaVector<E>.
 *
 * Ops per DeltaVector::remove/add/set/removeAll:
 *   0 remove(index)  1 add(index, item)  2 set(index, item)  4 removeAll()
 */
template<typename T, typename FReadItem>
TSWGListChanges<T> ReadDeltaVectorChanges(FSWGPacket& Packet, FReadItem ReadItem)
{
	return ReadListChanges<T>(Packet, [&ReadItem](FSWGPacket& P)
	{
		TSWGListChange<T> Change;

		const uint8 Op = P.ReadByte();
		switch (Op)
		{
			case 0x00:
				Change.Operation = ESWGListChangeOperation::Remove;
				Change.Index = P.ReadInt16();
				break;
			case 0x01:
				Change.Operation = ESWGListChangeOperation::Add;
				Change.Index = P.ReadInt16();
				Change.Value = ReadItem(P);
				break;
			case 0x02:
				Change.Operation = ESWGListChangeOperation::Set;
				Change.Index = P.ReadInt16();
				Change.Value = ReadItem(P);
				break;
			case 0x04:
				Change.Operation = ESWGListChangeOperation::ClearAll;
				break;
			default:
				UE_LOG(LogTemp, Warning, TEXT("ReadDeltaVectorChanges: unknown list change operation 0x%02X — stopping"), Op);
				break;
		}

		return Change;
	});
}

/**
 * Value-addressed changes, for fields the server keeps in a DeltaSet<K,V>.
 * ReadItem reads whatever that field's op carries — the key alone for
 * add/drop, key+value for addWithKey/dropByValue.
 *
 * Ops per DeltaSet::add/drop/removeAll:
 *   0 drop(item)  1 add(item)  2 removeAll()
 */
template<typename T, typename FReadItem>
TSWGListChanges<T> ReadDeltaSetChanges(FSWGPacket& Packet, FReadItem ReadItem)
{
	return ReadListChanges<T>(Packet, [&ReadItem](FSWGPacket& P)
	{
		TSWGListChange<T> Change;

		const uint8 Op = P.ReadByte();
		switch (Op)
		{
			case 0x00:
				Change.Operation = ESWGListChangeOperation::Remove;
				Change.Value = ReadItem(P);
				break;
			case 0x01:
				Change.Operation = ESWGListChangeOperation::Add;
				Change.Value = ReadItem(P);
				break;
			case 0x02:
				Change.Operation = ESWGListChangeOperation::ClearAll;
				break;
			default:
				UE_LOG(LogTemp, Warning, TEXT("ReadDeltaSetChanges: unknown list change operation 0x%02X — stopping"), Op);
				break;
		}

		return Change;
	});
}

/**
 * Value-addressed changes, for fields the server keeps in a DeltaVectorMap<K,V>.
 * Every op carries key+value (drop included), so ReadItem reads the pair.
 *
 * Ops per DeltaMapCommands — note ADD/DROP are the reverse of DeltaSet's:
 *   0 ADD(key, value)  1 DROP(key, value)  2 SET(key, value)
 */
template<typename T, typename FReadItem>
TSWGListChanges<T> ReadDeltaVectorMapChanges(FSWGPacket& Packet, FReadItem ReadItem)
{
	return ReadListChanges<T>(Packet, [&ReadItem](FSWGPacket& P)
	{
		TSWGListChange<T> Change;

		const uint8 Op = P.ReadByte();
		switch (Op)
		{
			case 0x00:
				Change.Operation = ESWGListChangeOperation::Add;
				Change.Value = ReadItem(P);
				break;
			case 0x01:
				Change.Operation = ESWGListChangeOperation::Remove;
				Change.Value = ReadItem(P);
				break;
			case 0x02:
				Change.Operation = ESWGListChangeOperation::Set;
				Change.Value = ReadItem(P);
				break;
			default:
				UE_LOG(LogTemp, Warning, TEXT("ReadDeltaVectorMapChanges: unknown list change operation 0x%02X — stopping"), Op);
				break;
		}

		return Change;
	});
}

// ── Primitive conveniences ─────────────────────────────────────────
// The shapes that come up often enough not to want a lambda at every call site.

inline TSWGListChanges<int32> ReadInt32DeltaVectorChanges(FSWGPacket& Packet)
{
	return ReadDeltaVectorChanges<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });
}

inline TSWGListChanges<int64> ReadInt64DeltaVectorChanges(FSWGPacket& Packet)
{
	return ReadDeltaVectorChanges<int64>(Packet, [](FSWGPacket& P) { return P.ReadInt64(); });
}

inline TSWGListChanges<float> ReadFloatDeltaVectorChanges(FSWGPacket& Packet)
{
	return ReadDeltaVectorChanges<float>(Packet, [](FSWGPacket& P) { return P.ReadFloat(); });
}

inline TSWGListChanges<FString> ReadAsciiStringDeltaSetChanges(FSWGPacket& Packet)
{
	return ReadDeltaSetChanges<FString>(Packet, [](FSWGPacket& P) { return P.ReadAsciiString(); });
}

inline TSWGListChanges<FString> ReadUnicodeStringDeltaSetChanges(FSWGPacket& Packet)
{
	return ReadDeltaSetChanges<FString>(Packet, [](FSWGPacket& P) { return P.ReadUnicodeString(); });
}

// ── Applying changes to a held collection ──────────────────────────

/** Applies position-addressed changes (from ReadDeltaVectorChanges) to List. */
template<typename T>
void ApplyIndexedListChanges(const TSWGListChanges<T>& Changes, TSWGBaselineList<T>& List)
{
	for (const TSWGListChange<T>& Change : Changes.Changes)
	{
		switch (Change.Operation)
		{
			case ESWGListChangeOperation::Add:
				if (List.Items.IsValidIndex(Change.Index))
				{
					List.Items.Insert(Change.Value, Change.Index);
				}
				else
				{
					List.Items.Add(Change.Value);
				}
				break;

			case ESWGListChangeOperation::Set:
				if (List.Items.IsValidIndex(Change.Index))
				{
					List.Items[Change.Index] = Change.Value;
				}
				else
				{
					List.Items.Add(Change.Value);
				}
				break;

			case ESWGListChangeOperation::Remove:
				if (List.Items.IsValidIndex(Change.Index))
				{
					List.Items.RemoveAt(Change.Index);
				}
				break;

			case ESWGListChangeOperation::ClearAll:
				List.Items.Empty();
				break;
		}
	}

	List.UpdateCounter = Changes.UpdateCounter;
}

/**
 * Applies value-addressed changes (from ReadDeltaSetChanges or
 * ReadDeltaVectorMapChanges) to List. Matches compares a held item against a
 * changed one to find what an op refers to — usually a key field, since the
 * value half of a pair is what's being replaced.
 */
template<typename T, typename FMatches>
void ApplyKeyedListChanges(const TSWGListChanges<T>& Changes, TSWGBaselineList<T>& List, FMatches Matches)
{
	for (const TSWGListChange<T>& Change : Changes.Changes)
	{
		const int32 Existing = List.Items.IndexOfByPredicate([&Change, &Matches](const T& Item)
		{
			return Matches(Item, Change.Value);
		});

		switch (Change.Operation)
		{
			case ESWGListChangeOperation::Add:
			case ESWGListChangeOperation::Set:
				if (Existing != INDEX_NONE)
				{
					List.Items[Existing] = Change.Value;
				}
				else
				{
					List.Items.Add(Change.Value);
				}
				break;

			case ESWGListChangeOperation::Remove:
				if (Existing != INDEX_NONE)
				{
					List.Items.RemoveAt(Existing);
				}
				break;

			case ESWGListChangeOperation::ClearAll:
				List.Items.Empty();
				break;
		}
	}

	List.UpdateCounter = Changes.UpdateCounter;
}
