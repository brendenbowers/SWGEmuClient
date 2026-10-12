#pragma once

#include "CoreMinimal.h"

class UReverbEffect;

/**
 * interior.iff "Room Type" is an enum of 26 reverb presets (alley=0 ... underWater=25, read from the
 * column's TYPE chunk). Each maps to a UReverbEffect with that preset's I3DL2 / EAX 2 parameters
 * (dB values converted to linear), since UReverbEffect exposes the same parameter set.
 */
namespace SWGReverbPresets
{
	constexpr int32 PresetCount = 26;

	/** The enum name for a Room Type ("sewerPipe"), or "unknown" out of range. */
	SWGEMUCLIENT_API const TCHAR* GetRoomTypeName(int32 RoomType);

	/** A cached transient effect for the preset, owned by Outer. Null for an out-of-range value. */
	SWGEMUCLIENT_API UReverbEffect* GetReverbEffect(int32 RoomType, UObject* Outer);
}
