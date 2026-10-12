#pragma once

#include "CoreMinimal.h"

/**
 * One row of datatables/environment/<planet>.iff: what an environment family looks like, sounds like
 * and does in one weather state (5 rows per family, Weather Index 0..4). Loaded by
 * USWGAmbienceSubsystem::LoadEnvironmentTable, picked with FindEnvironmentRow, and consumed by the
 * ambience (sounds), terrain (sky, fog, clouds, ramp) and weather (camera particle, wind) subsystems.
 */
struct FSWGEnvironmentRow
{
	FString Family;
	int32 WeatherIndex = 0;

	// Sky. Paths use '/' and may be empty (the row then adds nothing of its own).
	FString GradientSky;
	FString ColorRamp;
	FString CloudBottomShader;
	float CloudBottomSize = 0.f;
	float CloudBottomSpeed = 0.f;
	FString CloudTopShader;
	float CloudTopSize = 0.f;
	float CloudTopSpeed = 0.f;
	FString DayEnvironmentMap;
	FString NightEnvironmentMap;

	bool bFogEnabled = true;
	float MinFogDensity = 0.f;
	float MaxFogDensity = 0.f;

	/** The weather .prt that follows the camera (rain sheets, dust storm, fog, falling leaves); empty for none. */
	FString CameraParticle;
	/** Multiplies the server's wind vector into m/s (Tatooine: 1, 5, 7, 11, 15 over the five weathers). */
	float WindSpeedScale = 0.f;

	FString DayAmbient[2];
	FString NightAmbient[2];
	FString FirstMusic;
	FString SunriseMusic;
	FString SunsetMusic;

	/** Fog density this row asks for: the table's minimum in clear weather, rising to its maximum in the worst storm. */
	float GetFogDensity() const
	{
		return bFogEnabled ? FMath::Lerp(MinFogDensity, MaxFogDensity, FMath::Clamp(WeatherIndex, 0, 4) / 4.f) : 0.f;
	}
};
