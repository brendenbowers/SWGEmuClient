

#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * 
 */
struct SWGTRE_API FSWGIFFChunkReader
{
public:
	FSWGIFFChunkReader(const FSWGIffChunk& Chunk, const uint8* Data)
		: Chunk(Chunk)
		, Data(Data)
		, Position(0)
	{}

	FSWGIFFChunkReader(const FSWGIffChunk& Chunk, const FSWGIffReader& Reader)
		: FSWGIFFChunkReader(Chunk, Chunk.DataSize > 0 ? Reader.GetChunkData(Chunk) : nullptr)
	{}

	bool IsValid() { return Data != nullptr; }

	bool ReadTerminiatedString(FString& Value);

	bool ReadTerminatedStrings(TArray<FString>& Values, int32 Count = -1);
	TArray<FString> ReadTerminatedStrings(int32 Count = -1)
	{
		TArray<FString> Values;
		if (Count != -1)
		{
			Values.Reserve(Count);
		}

		ReadTerminatedStrings(Values, Count);
		return Values;
	}

	int32 GetPosition() { return Position; }

	bool Skip(int32 Skip) 
	{
		if (Position + Skip >= Chunk.DataSize)
		{
			return false;
		}

		Position += Skip;
		return true;
	}

	bool SkipString();


	bool CanRead(int32 Size)
	{
		return Position + Size <= Chunk.DataSize;
	}

	template<typename TValueType>
	bool CanRead()
	{
		return CanRead(sizeof(TValueType));
	}

	bool AtEnd() { return Position >= Chunk.DataSize; }

	FString ReadTerminiatedString() 
	{
		FString Value;
		ReadTerminiatedString(Value);
		return Value;
	}

	template<typename  TValueType>
	bool ReadValueLE(TValueType& Value)
	{
		constexpr int32 ValueSize = sizeof(TValueType);
		if (!Data || Position < 0 || ValueSize > Chunk.DataSize - Position)
		{
			return false;
		}
		FMemory::Memcpy(&Value, Data + Position, ValueSize);
		Position += ValueSize;

		return true;
	}

	template<typename TValueType>
	bool Skip()
	{
		return Skip(sizeof(TValueType));
	}

	template<typename TValueType>
	TValueType ReadValueLE()
	{
		TValueType Value;
		ReadValueLE(Value);
		return Value;
	}

	template<typename TCountType, typename TValueType>
	bool ReadArray(TArray<TValueType>& Value, TFunction<bool(TValueType&)> ReadFn)
	{
		TCountType Count;
		if (!ReadValueLE(Count))
		{
			return false;
		}

		if (Count > Chunk.DataSize - Position)
		{
			return false;
		}
		Value.Reset();
		Value.Reserve(Count);
		for (TCountType i = 0; i < Count; i++)
		{
			TValueType ReadVal;
			// read should be incrementing the position
			if (!ReadFn(ReadVal))
			{
				return false;
			}

			Value.Add(MoveTemp(ReadVal));
		}
		return true;
	}

	template<typename TCountType, typename TValueType>
	TArray<TValueType> ReadArray(TFunction<bool(TValueType&)> ReadFn)
	{
		TArray<TValueType> Value;
		ReadArray(Value, ReadFn);
		return Value;
	}


	template<typename TCountType, typename TKeyType, typename TValueType>
	bool ReadMap(TMap<TKeyType, TValueType>& Value, TFunction<bool(TKeyType&)> ReadKeyFn, TFunction<bool(TValueType&)> ReadValueFn)
	{
		TCountType Count;
		if (!ReadValueLE(Count))
		{
			return false;
		}


		if (Count > Chunk.DataSize - Position)
		{
			return false;
		}
		Value.Reset();
		Value.Reserve(Count);
		for (TCountType i = 0; i < Count; i++)
		{
			TKeyType ReadKeyVal;
			// read should be incrementing the position
			if (!ReadKeyFn(ReadKeyVal))
			{
				return false;
			}

			TValueType ReadVal;
			// read should be incrementing the position
			if (!ReadValueFn(ReadVal))
			{
				return false;
			}

			Value.Add(ReadKeyVal, ReadVal);
		}

		return true;
	}

	template<typename TCountType, typename TKeyType, typename TValueType>
	TMap<TKeyType, TValueType> ReadMap(TFunction<bool(TKeyType&)> ReadKeyFn, TFunction<bool(TValueType&)> ReadValueFn)
	{
		TMap<TKeyType, TValueType> Value;
		ReadMap(Value, ReadKeyFn, ReadValueFn);
		return Value;
	}
	
	template<typename TVectorType, typename TComponentType>
	bool ReadVectorLE(TVectorType& Value, TComponentType WorldScale)
	{
		Value.X = ReadValueLE<TComponentType>();
		Value.Z = ReadValueLE<TComponentType>();
		Value.Y = ReadValueLE<TComponentType>();

		Value *= WorldScale;
		return true;
	}

	template<typename TVectorType>
	bool ReadVectorLE(TVectorType& Value, TVectorType::FReal WorldScale)
	{
		return ReadVectorLE<TVectorType, typename TVectorType::FReal> (Value, WorldScale);
	}

	template<typename TVectorType, typename TComponentType>
	TVectorType ReadVectorLE(TComponentType WorldScale)
	{
		TVectorType Value;
		ReadVectorLE<TVectorType, TComponentType>(Value, WorldScale);
		return Value;
	}

	// Quaternions are stored (W,X,Y,Z) — confirmed against the root joint's
	// BPRO/RPRE, which is (1,0,0,0) on disk; that's only the identity
	// quaternion if the first float is W, not X. Applies the same Y/Z swap
	// on the vector (X,Y,Z) part only.
	template<typename TQuatType, typename TComponentType>
	bool ReadQuatLE(TQuatType& Value)
	{
		Value.W = ReadValueLE<TComponentType>();
		Value.X = ReadValueLE<TComponentType>();
		Value.Z = ReadValueLE<TComponentType>();
		Value.Y = ReadValueLE<TComponentType>();
		return true;
	}

	template<typename TQuatType>
	bool ReadQuatLE(TQuatType& Value)
	{
		return ReadQuatLE<TQuatType, typename TQuatType::FReal>(Value);
	}

	template<typename TQuatType, typename TComponentType>
	TQuatType ReadQuatLE()
	{
		TQuatType Value;
		ReadQuatLE<TQuatType, typename TQuatType::FReal>(Value);
		return Value;
	}


protected:
	const FSWGIffChunk& Chunk;
	const uint8* Data;
	int32 Position;
};
