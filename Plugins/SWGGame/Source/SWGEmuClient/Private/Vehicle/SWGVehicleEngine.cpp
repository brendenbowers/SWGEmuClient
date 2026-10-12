#include "Vehicle/SWGVehicleEngine.h"

FSWGVehicleEngineSounds FSWGVehicleEngineSounds::FromClientData(const FSWGClientDataFile& Data)
{
	FSWGVehicleEngineSounds Sounds;
	for (const FSWGClientDataThrust& Thrust : Data.Thrusts)
	{
		if (Sounds.Idle.IsEmpty()) { Sounds.Idle = Thrust.IdleSound; }
		if (Sounds.Accel.IsEmpty()) { Sounds.Accel = Thrust.AccelSound; }
		if (Sounds.Decel.IsEmpty()) { Sounds.Decel = Thrust.DecelSound; }
		if (Sounds.Run.IsEmpty()) { Sounds.Run = Thrust.RunSound; }
	}
	return Sounds;
}

FSWGEngineStep FSWGVehicleEngine::Update(ESWGEngineState Current, float SpeedFraction)
{
	FSWGEngineStep Step;
	Step.State = Current;
	if (Current == ESWGEngineState::Idle && SpeedFraction > RunAbove)
	{
		Step.State = ESWGEngineState::Running;
		Step.bPlayAccel = true;
	}
	else if (Current == ESWGEngineState::Running && SpeedFraction < IdleBelow)
	{
		Step.State = ESWGEngineState::Idle;
		Step.bPlayDecel = true;
	}
	return Step;
}

FString FSWGVehicleEngine::FindClientDataPath(const FSWGDataTableData& SaddleAppearanceMap, const FString& SkeletalAppearancePath)
{
	const FString BaseName = FPaths::GetBaseFilename(SkeletalAppearancePath);
	if (!BaseName.StartsWith(TEXT("pv_")))
	{
		return FString();
	}
	const int32 Row = SaddleAppearanceMap.FindRowIndex(TEXT("logical_saddle_name"), TEXT("lookup/") + BaseName.RightChop(3));
	return SaddleAppearanceMap.GetCell(Row, TEXT("client_data_filename"));
}
