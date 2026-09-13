#include "TRE/SWGStringTableReader.h"

namespace
{
	constexpr uint32 StfMagic = 0x0000ABCD;

	struct FStfCursor
	{
		const TArray<uint8>& Data;
		int32 Position = 0;

		bool CanRead(int32 Bytes) const { return Position + Bytes <= Data.Num(); }

		bool ReadUInt8(uint8& Out)
		{
			if (!CanRead(1)) return false;
			Out = Data[Position++];
			return true;
		}

		bool ReadUInt32(uint32& Out)
		{
			if (!CanRead(4)) return false;
			Out = (uint32)Data[Position] | ((uint32)Data[Position + 1] << 8) | ((uint32)Data[Position + 2] << 16) | ((uint32)Data[Position + 3] << 24);
			Position += 4;
			return true;
		}

		bool ReadUtf16(uint32 Length, FString& Out)
		{
			if (Length > (uint32)MAX_int32 / 2 || !CanRead(Length * 2)) return false;
			Out.Reset(Length);
			for (uint32 CharIndex = 0; CharIndex < Length; ++CharIndex)
			{
				Out.AppendChar((TCHAR)((uint16)Data[Position] | ((uint16)Data[Position + 1] << 8)));
				Position += 2;
			}
			return true;
		}

		bool ReadAscii(uint32 Length, FString& Out)
		{
			if (Length > (uint32)MAX_int32 || !CanRead(Length)) return false;
			Out = FString::ConstructFromPtrSize((const ANSICHAR*)Data.GetData() + Position, Length);
			Position += Length;
			return true;
		}
	};
}

bool FSWGStringTableReader::Read(const TArray<uint8>& Data, FSWGStringTable& OutTable)
{
	FStfCursor Cursor{ Data };

	uint32 Magic, NextId, Count;
	uint8 Flags;
	if (!Cursor.ReadUInt32(Magic) || Magic != StfMagic || !Cursor.ReadUInt8(Flags) || !Cursor.ReadUInt32(NextId) || !Cursor.ReadUInt32(Count))
	{
		return false;
	}

	TMap<uint32, FString> ValuesById;
	ValuesById.Reserve(Count);
	for (uint32 EntryIndex = 0; EntryIndex < Count; ++EntryIndex)
	{
		uint32 Id, Crc, Length;
		FString Value;
		if (!Cursor.ReadUInt32(Id) || !Cursor.ReadUInt32(Crc) || !Cursor.ReadUInt32(Length) || !Cursor.ReadUtf16(Length, Value))
		{
			return false;
		}
		ValuesById.Add(Id, MoveTemp(Value));
	}

	OutTable.Entries.Reserve(Count);
	for (uint32 EntryIndex = 0; EntryIndex < Count; ++EntryIndex)
	{
		uint32 Id, Length;
		FString Key;
		if (!Cursor.ReadUInt32(Id) || !Cursor.ReadUInt32(Length) || !Cursor.ReadAscii(Length, Key))
		{
			return false;
		}
		if (FString* Value = ValuesById.Find(Id))
		{
			OutTable.Entries.Add(MoveTemp(Key), MoveTemp(*Value));
		}
	}

	return true;
}

bool FSWGStringTableReader::ParseStringId(const FString& Reference, FString& OutTable, FString& OutKey)
{
	int32 ColonIndex;
	if (!Reference.FindChar(TEXT(':'), ColonIndex))
	{
		return false;
	}

	const int32 TableStart = Reference.StartsWith(TEXT("@")) ? 1 : 0;
	OutTable = Reference.Mid(TableStart, ColonIndex - TableStart);
	OutKey = Reference.Mid(ColonIndex + 1);
	return !OutTable.IsEmpty() && !OutKey.IsEmpty();
}
