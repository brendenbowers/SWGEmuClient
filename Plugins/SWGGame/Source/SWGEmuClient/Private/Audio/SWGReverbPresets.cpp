#include "Audio/SWGReverbPresets.h"

#include "Sound/ReverbEffect.h"

namespace
{
	struct FPreset
	{
		const TCHAR* Name;
		float Density;
		float Diffusion;
		float Gain;
		float GainHF;
		float DecayTime;
		float DecayHFRatio;
		float ReflectionsGain;
		float ReflectionsDelay;
		float LateGain;
		float LateDelay;
	};

	// Indexed by the interior.iff enum value.
	const FPreset Presets[] = {
		{ TEXT("alley"),           1.00f, 1.00f, 0.316f, 0.86f, 1.49f, 0.86f, 0.25f, 0.007f, 1.00f, 0.011f },
		{ TEXT("arena"),           1.00f, 1.00f, 0.316f, 0.71f, 7.24f, 0.33f, 0.26f, 0.020f, 1.00f, 0.030f },
		{ TEXT("auditorium"),      1.00f, 1.00f, 0.316f, 0.76f, 4.32f, 0.59f, 0.40f, 0.020f, 0.72f, 0.030f },
		{ TEXT("bathRoom"),        1.00f, 1.00f, 0.316f, 0.25f, 1.49f, 0.54f, 0.65f, 0.007f, 3.27f, 0.011f },
		{ TEXT("carpetedHallway"), 1.00f, 1.00f, 0.316f, 0.01f, 0.30f, 0.10f, 0.12f, 0.002f, 0.44f, 0.030f },
		{ TEXT("cave"),            1.00f, 1.00f, 0.316f, 1.00f, 2.91f, 1.30f, 0.50f, 0.015f, 0.70f, 0.022f },
		{ TEXT("city"),            1.00f, 1.00f, 0.316f, 0.40f, 1.49f, 0.67f, 0.07f, 0.007f, 0.43f, 0.011f },
		{ TEXT("concertHall"),     1.00f, 1.00f, 0.316f, 0.56f, 3.92f, 0.70f, 0.24f, 0.020f, 1.00f, 0.029f },
		{ TEXT("dizzy"),           0.60f, 0.60f, 0.316f, 0.63f, 10.0f, 0.56f, 0.19f, 0.020f, 1.00f, 0.030f },
		{ TEXT("drugged"),         0.50f, 1.00f, 0.316f, 0.50f, 8.39f, 1.39f, 0.95f, 0.002f, 1.00f, 0.030f },
		{ TEXT("forest"),          1.00f, 0.79f, 0.316f, 0.02f, 1.49f, 0.54f, 0.05f, 0.162f, 0.89f, 0.088f },
		{ TEXT("generic"),         1.00f, 1.00f, 0.316f, 0.89f, 1.49f, 0.83f, 0.05f, 0.007f, 1.26f, 0.011f },
		{ TEXT("hallway"),         1.00f, 1.00f, 0.316f, 0.71f, 1.49f, 0.59f, 0.25f, 0.007f, 1.58f, 0.011f },
		{ TEXT("hangar"),          1.00f, 1.00f, 0.316f, 0.63f, 10.05f, 0.23f, 0.50f, 0.020f, 1.26f, 0.030f },
		{ TEXT("livingrRoom"),     1.00f, 1.00f, 0.316f, 0.00f, 0.50f, 0.10f, 0.21f, 0.003f, 0.28f, 0.004f },
		{ TEXT("mountains"),       1.00f, 0.27f, 0.316f, 0.06f, 1.49f, 0.21f, 0.04f, 0.300f, 0.19f, 0.100f },
		{ TEXT("paddedCell"),      1.00f, 1.00f, 0.316f, 0.00f, 0.17f, 0.10f, 0.25f, 0.001f, 1.27f, 0.002f },
		{ TEXT("parkingLot"),      1.00f, 1.00f, 0.316f, 1.00f, 1.65f, 1.50f, 0.21f, 0.008f, 0.27f, 0.012f },
		{ TEXT("plain"),           1.00f, 0.21f, 0.316f, 0.10f, 1.49f, 0.50f, 0.06f, 0.179f, 0.11f, 0.100f },
		{ TEXT("psychotic"),       0.50f, 0.50f, 0.316f, 0.60f, 7.56f, 0.91f, 0.44f, 0.020f, 1.00f, 0.030f },
		{ TEXT("quarry"),          1.00f, 1.00f, 0.316f, 0.32f, 1.49f, 0.83f, 0.03f, 0.061f, 1.78f, 0.025f },
		{ TEXT("room"),            1.00f, 1.00f, 0.316f, 0.59f, 0.40f, 0.83f, 0.15f, 0.002f, 1.06f, 0.003f },
		{ TEXT("sewerPipe"),       0.80f, 0.31f, 0.316f, 0.32f, 2.81f, 0.14f, 1.64f, 0.014f, 3.25f, 0.021f },
		{ TEXT("stoneCorridor"),   1.00f, 1.00f, 0.316f, 0.76f, 2.70f, 0.79f, 0.25f, 0.013f, 1.00f, 0.020f },
		{ TEXT("stoneRoom"),       1.00f, 1.00f, 0.316f, 0.71f, 2.31f, 0.64f, 0.44f, 0.012f, 1.10f, 0.017f },
		{ TEXT("underWater"),      0.36f, 1.00f, 0.316f, 0.01f, 1.49f, 0.10f, 0.60f, 0.007f, 7.08f, 0.011f },
	};
	static_assert(UE_ARRAY_COUNT(Presets) == SWGReverbPresets::PresetCount, "one preset per interior.iff Room Type value");
}

const TCHAR* SWGReverbPresets::GetRoomTypeName(int32 RoomType)
{
	return RoomType >= 0 && RoomType < PresetCount ? Presets[RoomType].Name : TEXT("unknown");
}

UReverbEffect* SWGReverbPresets::GetReverbEffect(int32 RoomType, UObject* Outer)
{
	if (RoomType < 0 || RoomType >= PresetCount || !Outer)
	{
		return nullptr;
	}

	const FName EffectName(*FString::Printf(TEXT("SWGReverb_%s"), Presets[RoomType].Name));
	if (UReverbEffect* Existing = FindObject<UReverbEffect>(Outer, *EffectName.ToString()))
	{
		return Existing;
	}

	const FPreset& Preset = Presets[RoomType];
	UReverbEffect* Effect = NewObject<UReverbEffect>(Outer, EffectName, RF_Transient);
	Effect->Density = Preset.Density;
	Effect->Diffusion = Preset.Diffusion;
	Effect->Gain = Preset.Gain;
	Effect->GainHF = Preset.GainHF;
	Effect->DecayTime = Preset.DecayTime;
	Effect->DecayHFRatio = Preset.DecayHFRatio;
	Effect->ReflectionsGain = Preset.ReflectionsGain;
	Effect->ReflectionsDelay = Preset.ReflectionsDelay;
	Effect->LateGain = Preset.LateGain;
	Effect->LateDelay = Preset.LateDelay;
	return Effect;
}
