

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
		if (!CanRead(Skip))
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

	/**
	 * [key value]... with no leading count — pairs run to the end of the
	 * chunk. Additive into Value so one map can be assembled from several
	 * sibling chunks (a .cdf's CSSI/WCSI chunks hold one pair each).
	 */
	template<typename TKeyType, typename TValueType>
	bool ReadMapToEnd(TMap<TKeyType, TValueType>& Value, TFunction<bool(TKeyType&)> ReadKeyFn, TFunction<bool(TValueType&)> ReadValueFn)
	{
		if (!Data)
		{
			return false;
		}

		while (!AtEnd())
		{
			TKeyType ReadKeyVal;
			TValueType ReadVal;
			if (!ReadKeyFn(ReadKeyVal) || !ReadValueFn(ReadVal))
			{
				return false;
			}
			Value.Add(MoveTemp(ReadKeyVal), MoveTemp(ReadVal));
		}
		return true;
	}
	
	/**
	 * SWG native (x right, y up, z forward) -> UE (X forward, Y right, Z up):
	 * swg(x,y,z) -> ue(z, x, y). Both frames are left-handed and this is a
	 * cyclic permutation, i.e. a proper rotation — see SWGWorldScale.h. A
	 * plain Y/Z swap would be a reflection and mirror the world.
	 */
	template<typename TVectorType, typename TComponentType>
	bool ReadVectorLE(TVectorType& Value, TComponentType WorldScale)
	{
		Value.Y = ReadValueLE<TComponentType>();
		Value.Z = ReadValueLE<TComponentType>();
		Value.X = ReadValueLE<TComponentType>();

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

	// Quaternions are stored (W,X,Y,Z). The axis map is a proper rotation, so
	// the vector part maps exactly like a position (no conjugate needed) —
	// FSWGAnimationReader decodes .ans samples the same way.
	template<typename TQuatType, typename TComponentType>
	bool ReadQuatLE(TQuatType& Value)
	{
		Value.W = ReadValueLE<TComponentType>();
		Value.Y = ReadValueLE<TComponentType>();
		Value.Z = ReadValueLE<TComponentType>();
		Value.X = ReadValueLE<TComponentType>();
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

	template<typename TTransformType, typename TComponentType>
	bool ReadTransform(TTransformType& OutTransform, TComponentType WorldScale)
	{
		using FMatrixType = UE::Math::TMatrix<typename TTransformType::FReal>;
		using FVectorType = UE::Math::TVector<typename TTransformType::FReal>;


		TComponentType T[3] = {};
		TComponentType R[3][3] = {};
		bool bReadOk = true;
		for (int32 Row = 0; Row < 3 && bReadOk; ++Row)
		{
			bReadOk = ReadValueLE(R[Row][0])
				&& ReadValueLE(R[Row][1])
				&& ReadValueLE(R[Row][2])
				&& ReadValueLE(T[Row]);
		}
		if (!bReadOk)
		{
			return false;
		}

		// Conjugation by ReadVectorLE's axis permutation (SwgAxis[ue] = native
		// axis) AND a transpose — note the [j]/[i] order.
		//
		// The transpose is not cosmetic: SWG stores this 3x4 as three rows of
		// [Rx Ry Rz T], where the basis axes are the matrix's COLUMNS
		// (world = R * local, column-vector convention). UE's FMatrix is
		// row-vector convention (v' = v * M), where the basis axes are its
		// ROWS. Copying straight across therefore stored the transpose of a
		// rotation — i.e. its inverse.
		static constexpr int32 SwgAxis[3] = { 2, 0, 1 };
		FMatrixType RotationMatrix = FMatrixType::Identity;

		for (int32 i = 0; i < 3; ++i)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				RotationMatrix.M[i][j] = R[SwgAxis[j]][SwgAxis[i]];
			}
		}

		OutTransform.SetRotation(RotationMatrix.ToQuat());
		OutTransform.SetTranslation(FVectorType(T[SwgAxis[0]], T[SwgAxis[1]], T[SwgAxis[2]]) * WorldScale);
		OutTransform.SetScale3D(FVectorType::OneVector);
		return true;
	}

	template<typename TTransformType>
	bool ReadTransform(TTransformType& OutTransform, typename TTransformType::FReal WorldScale)
	{
		return ReadTransform<TTransformType, typename TTransformType::FReal>(OutTransform, WorldScale);
	}

	template<typename TTransformType, typename TComponentType>
	TTransformType ReadTransform(TComponentType WorldScale)
	{
		TTransformType OutTransform;
		ReadTransform<TTransformType, TComponentType>(OutTransform, WorldScale);
		return OutTransform;
	}

protected:
	const FSWGIffChunk& Chunk;
	const uint8* Data;
	int32 Position;
};
