#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/** Core3 PlayMusicMessage: a .snd for the music channel; "sound/music_silence.snd" stops it. */
struct SWGEMU_API FPlayMusicMessage : public FSWGNetMessage
{
	FString SoundFile;
	/** Core3 writes a target object (usually 0); its meaning is unknown. */
	uint64 Target = 0;
	uint32 UnknownInt = 0;
	uint8 UnknownBool = 0;

	FPlayMusicMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};

/** Core3 PlayClientEffectObjectMessage: a .cef on an object, optionally at a named hardpoint ("head"). */
struct SWGEMU_API FPlayClientEffectObjectMessage : public FSWGNetMessage
{
	FString EffectFile;
	/** Hardpoint name, empty for the object's root. */
	FString Hardpoint;
	uint64 ObjectId = 0;

	FPlayClientEffectObjectMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};

/** Retail archive order: effect name, quaternion XYZW, native position XYZ (Y up), object ID. */
struct SWGEMU_API FPlayClientEffectObjectTransformMessage : public FSWGNetMessage
{
	FString EffectFile;
	float RotationX = 0.f, RotationY = 0.f, RotationZ = 0.f, RotationW = 1.f;
	float X = 0.f, Y = 0.f, Z = 0.f;
	uint64 ObjectId = 0;
	bool bValid = false;

	FPlayClientEffectObjectTransformMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { bValid = Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};

/**
 * Core3 PlayClientEffectLoc. Position is wire order x, z, y (raw: x east, z up, y north),
 * relative to the cell when CellId is non-zero.
 */
struct SWGEMU_API FPlayClientEffectLocMessage : public FSWGNetMessage
{
	FString EffectFile;
	FString Planet;
	float X = 0.f;
	float Z = 0.f;
	float Y = 0.f;
	uint64 CellId = 0;
	uint32 TerrainDelta = 0;
	FString Label;

	FPlayClientEffectLocMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};

/** Core3 PlayClientEventObjectMessage; the server never sends it, so it is only logged. */
struct SWGEMU_API FPlayClientEventObjectMessage : public FSWGNetMessage
{
	FString EventName;
	FString Aux;
	uint64 ObjectId = 0;

	FPlayClientEventObjectMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};
