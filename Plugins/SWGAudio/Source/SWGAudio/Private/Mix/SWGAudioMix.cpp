#include "Mix/SWGAudioMix.h"

#include "AudioDevice.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundMix.h"

namespace
{
	constexpr float CentimetersPerMeter = 100.f;

	/** Retail category values that get their own class and concurrency group. */
	const int32 CategoryValues[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12 };

	int32 MaxVoicesForCategory(int32 CategoryValue)
	{
		switch (static_cast<ESWGSoundCategory>(CategoryValue))
		{
		case ESWGSoundCategory::Ambient: return 24;
		case ESWGSoundCategory::Explosion: return 6;
		case ESWGSoundCategory::Item: return 6;
		case ESWGSoundCategory::Movement: return 8; // footsteps
		case ESWGSoundCategory::UI: return 8;
		case ESWGSoundCategory::Vehicle: return 6;
		case ESWGSoundCategory::Creature: return 10;
		case ESWGSoundCategory::Weapon: return 8;
		case ESWGSoundCategory::BackgroundMusic: return 3; // outgoing + incoming crossfade
		case ESWGSoundCategory::PlayerMusic: return 4;
		case ESWGSoundCategory::Machine: return 8;
		case ESWGSoundCategory::Special: return 2;
		default: return 8;
		}
	}
}

USoundClass* USWGAudioMix::MakeClass(const TCHAR* Name, USoundClass* Parent)
{
	USoundClass* SoundClass = NewObject<USoundClass>(this, FName(Name));
	if (Parent)
	{
		SoundClass->ParentClass = Parent;
		Parent->ChildClasses.Add(SoundClass);
	}
	return SoundClass;
}

void USWGAudioMix::Build()
{
	const int32 BusCount = static_cast<int32>(ESWGAudioBus::Count);
	BusVolumes.Init(1.f, BusCount);
	BusClasses.SetNum(BusCount);

	BusClasses[static_cast<int32>(ESWGAudioBus::Master)] = MakeClass(TEXT("SWG_Master"), nullptr);
	USoundClass* Master = BusClasses[static_cast<int32>(ESWGAudioBus::Master)];
	for (int32 BusIndex = 1; BusIndex < BusCount; ++BusIndex)
	{
		const ESWGAudioBus Bus = static_cast<ESWGAudioBus>(BusIndex);
		BusClasses[BusIndex] = MakeClass(*FString::Printf(TEXT("SWG_Bus_%s"), SWGAudio::GetBusName(Bus)), Master);
	}

	for (const int32 CategoryValue : CategoryValues)
	{
		USoundClass* Parent = GetBusClass(SWGAudio::GetBusForCategory(CategoryValue));
		CategoryClasses.Add(CategoryValue, MakeClass(*FString::Printf(TEXT("SWG_Category_%d"), CategoryValue), Parent));

		USoundConcurrency* Concurrency = NewObject<USoundConcurrency>(this, FName(*FString::Printf(TEXT("SWG_Concurrency_%d"), CategoryValue)));
		Concurrency->Concurrency.MaxCount = MaxVoicesForCategory(CategoryValue);
		Concurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopFarthestThenOldest;
		CategoryConcurrency.Add(CategoryValue, Concurrency);
	}

	VoiceClass = MakeClass(TEXT("SWG_Voice"), GetBusClass(ESWGAudioBus::Creature));
	VoiceConcurrency = NewObject<USoundConcurrency>(this, TEXT("SWG_Concurrency_Voice"));
	VoiceConcurrency->Concurrency.MaxCount = 4;
	VoiceConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;

	Mix = NewObject<USoundMix>(this, TEXT("SWG_Mix"));
}

USoundClass* USWGAudioMix::GetBusClass(ESWGAudioBus Bus) const
{
	return BusClasses.IsValidIndex(static_cast<int32>(Bus)) ? BusClasses[static_cast<int32>(Bus)].Get() : nullptr;
}

USoundClass* USWGAudioMix::GetCategoryClass(int32 CategoryValue, bool bVoice) const
{
	if (bVoice)
	{
		return VoiceClass;
	}
	const TObjectPtr<USoundClass>* Found = CategoryClasses.Find(CategoryValue);
	return Found ? Found->Get() : GetBusClass(ESWGAudioBus::Effects);
}

USoundConcurrency* USWGAudioMix::GetConcurrency(int32 CategoryValue, bool bVoice) const
{
	if (bVoice)
	{
		return VoiceConcurrency;
	}
	const TObjectPtr<USoundConcurrency>* Found = CategoryConcurrency.Find(CategoryValue);
	return Found ? Found->Get() : nullptr;
}

USoundAttenuation* USWGAudioMix::GetAttenuation(float DistanceMeters)
{
	const float FullVolumeMeters = DistanceMeters > 0.f ? DistanceMeters : DefaultDistanceMeters;
	const int32 Key = FMath::RoundToInt(FullVolumeMeters);
	if (const TObjectPtr<USoundAttenuation>* Found = AttenuationByMeters.Find(Key))
	{
		return Found->Get();
	}

	// Retail (Miles 3D): min distance = template distance, max = 4x, volume = min / distance between,
	// and the client stops sounds beyond max (SWGEmu.exe 0x40f590, 0x412a00). The curve input is 0..1
	// across the falloff band, so distance = inner * (1 + 3u); the last key is the beyond-max cull.
	const float InnerCm = Key * CentimetersPerMeter;
	USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(this);
	FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
	Settings.bAttenuate = true;
	Settings.bSpatialize = true;
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::Custom;
	Settings.AttenuationShape = EAttenuationShape::Sphere;
	Settings.AttenuationShapeExtents = FVector(InnerCm, 0.f, 0.f);
	Settings.FalloffDistance = InnerCm * (AttenuationFalloffFactor - 1.f);
	FRichCurve* Curve = Settings.CustomAttenuationCurve.GetRichCurve();
	constexpr int32 CurveSegments = 16;
	for (int32 Segment = 0; Segment <= CurveSegments; ++Segment)
	{
		const float Position = static_cast<float>(Segment) / CurveSegments * 0.999f;
		Curve->AddKey(Position, 1.f / (1.f + (AttenuationFalloffFactor - 1.f) * Position));
	}
	Curve->AddKey(1.f, 0.f);
	AttenuationByMeters.Add(Key, Attenuation);
	return Attenuation;
}

void USWGAudioMix::SetBusVolume(ESWGAudioBus Bus, float Volume)
{
	if (BusVolumes.IsValidIndex(static_cast<int32>(Bus)))
	{
		BusVolumes[static_cast<int32>(Bus)] = FMath::Max(0.f, Volume);
	}
}

void USWGAudioMix::ApplyToDevice(FAudioDevice* Device)
{
	if (!Device || !Mix)
	{
		return;
	}

	if (AppliedDeviceId != Device->DeviceID)
	{
		for (USoundClass* BusClass : BusClasses)
		{
			Device->RegisterSoundClass(BusClass);
		}
		for (const TPair<int32, TObjectPtr<USoundClass>>& Entry : CategoryClasses)
		{
			Device->RegisterSoundClass(Entry.Value);
		}
		Device->RegisterSoundClass(VoiceClass);
		Device->PushSoundMixModifier(Mix);
		AppliedDeviceId = Device->DeviceID;
	}

	for (int32 BusIndex = 0; BusIndex < BusClasses.Num(); ++BusIndex)
	{
		Device->SetSoundMixClassOverride(Mix, BusClasses[BusIndex], BusVolumes[BusIndex], 1.f, 0.f, true);
	}
}
