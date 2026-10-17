#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGDataTableReader.h"

enum class ESWGEngineState : uint8
{
	Idle,
	Running,
};

/** The four engine sounds a vehicle's VTHR/VSND names (the same in every thrust level). */
struct SWGEMUCLIENT_API FSWGVehicleEngineSounds
{
	FString Idle;
	FString Accel;
	FString Decel;
	FString Run;

	bool IsValid() const { return !Idle.IsEmpty() || !Run.IsEmpty(); }

	/** First non-empty value per slot across the VTHR levels. */
	static FSWGVehicleEngineSounds FromClientData(const FSWGClientDataFile& Data);
};

struct SWGEMUCLIENT_API FSWGEngineStep
{
	ESWGEngineState State = ESWGEngineState::Idle;
	bool bPlayAccel = false;
	bool bPlayDecel = false;
};

struct SWGEMUCLIENT_API FSWGVehicleEngine
{
	/** Speed fractions with hysteresis so a vehicle hovering at the threshold doesn't flutter between loops. */
	static constexpr float RunAbove = 0.06f;
	static constexpr float IdleBelow = 0.02f;

	static FSWGEngineStep Update(ESWGEngineState Current, float SpeedFraction);

	/**
	 * datatables/mount/saddle_appearance_map.iff row for a vehicle's "pv_*.sat" appearance
	 * (logical_saddle_name "lookup/<name>") -> its client_data_filename, or empty.
	 */
	static FString FindClientDataPath(const FSWGDataTableData& SaddleAppearanceMap, const FString& SkeletalAppearancePath);
};
