#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGDataTableReader.h"
#include "Vehicle/SWGVehicleEngine.h"
#include "Vehicle/SWGVehicleHoverDynamics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	USWGTreSubsystem* LoadVehicleTre(FAutomationTestBase& Test)
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
		USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
		if (!Tre)
		{
			Test.AddError(TEXT("could not construct a TRE subsystem"));
			return nullptr;
		}
		Tre->AddToRoot();
		Tre->LoadConfig();
		if (!Tre->LoadArchives())
		{
			Test.AddError(TEXT("no .tre archives loaded - check TreDirectory in DefaultGame.ini"));
			Tre->RemoveFromRoot();
			return nullptr;
		}
		return Tre;
	}

	FSWGHoverOutput Run(const FSWGHoverParams& Params, FSWGHoverState& State, float Seconds, float Speed, float SpeedPerSecond, float TurnRadiansPerSecond, float StartHeading = 0.f)
	{
		constexpr float Step = 1.f / 60.f;
		FSWGHoverOutput Output;
		float Heading = StartHeading;
		for (float Time = 0.f; Time < Seconds; Time += Step)
		{
			Speed = FMath::Max(Speed + SpeedPerSecond * Step, 0.f);
			Heading += TurnRadiansPerSecond * Step;
			FSWGHoverInput Input;
			Input.DeltaSeconds = Step;
			Input.Speed = Speed;
			Input.Forward = FVector2D(FMath::Cos(Heading), FMath::Sin(Heading));
			Output = FSWGVehicleHoverDynamics::Step(Params, State, Input);
		}
		return Output;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGHoverMathTest, "SWGEmu.Vehicle.HoverMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGHoverMathTest::RunTest(const FString& Parameters)
{
	const FSWGHoverParams Params;

	{
		FSWGHoverState State;
		const FSWGHoverOutput Cruise = Run(Params, State, 2.f, 1000.f, 0.f, 0.f);
		TestTrue(TEXT("steady straight cruise stays level"), FMath::Abs(Cruise.Pitch) < 0.001f && FMath::Abs(Cruise.Roll) < 0.001f);
	}
	{
		FSWGHoverState State;
		const FSWGHoverOutput Launch = Run(Params, State, 0.5f, 0.f, 600.f, 0.f);
		TestTrue(TEXT("accelerating from rest pitches nose up"), Launch.Pitch > 0.01f);
		TestTrue(TEXT("pitch is clamped"), Launch.Pitch <= Params.MaxAngle);
	}
	{
		FSWGHoverState State;
		Run(Params, State, 0.2f, 1200.f, 0.f, 0.f);
		const FSWGHoverOutput Brake = Run(Params, State, 0.5f, 1200.f, -1000.f, 0.f);
		TestTrue(TEXT("braking pitches nose down"), Brake.Pitch < -0.01f);
	}
	{
		FSWGHoverState Right;
		const FSWGHoverOutput RightTurn = Run(Params, Right, 1.f, 1000.f, 0.f, 0.8f);
		TestTrue(TEXT("a right turn leans right (positive roll)"), RightTurn.Roll > 0.02f);
		TestTrue(TEXT("turn fraction is reported"), RightTurn.TurnFraction > 0.1f);
		FSWGHoverState Left;
		const FSWGHoverOutput LeftTurn = Run(Params, Left, 1.f, 1000.f, 0.f, -0.8f);
		TestTrue(TEXT("a left turn leans left (negative roll)"), LeftTurn.Roll < -0.02f);
		TestTrue(TEXT("roll is symmetric"), FMath::IsNearlyEqual(RightTurn.Roll, -LeftTurn.Roll, 0.005f));
	}
	{
		// Terrain slope is followed, and flattened more when slow.
		FSWGHoverState Fast, Slow;
		FSWGHoverInput Input;
		Input.DeltaSeconds = 1.f / 60.f;
		Input.bHasGround = true;
		Input.GroundPitch = 0.3f;
		Input.Speed = Params.MaxSpeed;
		FSWGHoverOutput FastOutput, SlowOutput;
		for (int32 Index = 0; Index < 240; ++Index)
		{
			FastOutput = FSWGVehicleHoverDynamics::Step(Params, Fast, Input);
		}
		Input.Speed = 0.f;
		for (int32 Index = 0; Index < 240; ++Index)
		{
			SlowOutput = FSWGVehicleHoverDynamics::Step(Params, Slow, Input);
		}
		TestTrue(TEXT("fast vehicle matches the slope"), FMath::IsNearlyEqual(FastOutput.Pitch, 0.3f, 0.01f));
		TestTrue(TEXT("a stopped vehicle is flattened to half"), FMath::IsNearlyEqual(SlowOutput.Pitch, 0.15f, 0.01f));
	}
	{
		// Height lags a terrain step, then settles.
		FSWGHoverState State;
		FSWGHoverInput Input;
		Input.DeltaSeconds = 1.f / 60.f;
		Input.bHasGround = true;
		Input.Speed = 500.f;
		Input.GroundHeight = 0.f;
		FSWGVehicleHoverDynamics::Step(Params, State, Input);
		Input.GroundHeight = 100.f;
		const FSWGHoverOutput Stepped = FSWGVehicleHoverDynamics::Step(Params, State, Input);
		TestTrue(TEXT("body lags an upward step"), Stepped.HeightOffset < -50.f);
		FSWGHoverOutput Settled;
		for (int32 Index = 0; Index < 600; ++Index)
		{
			Settled = FSWGVehicleHoverDynamics::Step(Params, State, Input);
		}
		TestTrue(TEXT("height settles on the ground"), FMath::Abs(Settled.HeightOffset) < 1.f);
	}

	TestEqual(TEXT("blend at rest"), FSWGVehicleHoverDynamics::BlendBySpeed(2.f, 4.f, 0.f), 2.f);
	TestEqual(TEXT("blend at max"), FSWGVehicleHoverDynamics::BlendBySpeed(2.f, 4.f, 1.f), 4.f);
	TestEqual(TEXT("blend clamps"), FSWGVehicleHoverDynamics::BlendBySpeed(2.f, 4.f, 5.f), 4.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGVehicleEngineStateTest, "SWGEmu.Vehicle.EngineState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGVehicleEngineStateTest::RunTest(const FString& Parameters)
{
	FSWGEngineStep Step = FSWGVehicleEngine::Update(ESWGEngineState::Idle, 0.04f);
	TestEqual(TEXT("below the run threshold stays idle"), Step.State, ESWGEngineState::Idle);
	TestFalse(TEXT("no accel sound yet"), Step.bPlayAccel);

	Step = FSWGVehicleEngine::Update(ESWGEngineState::Idle, 0.5f);
	TestEqual(TEXT("moving starts running"), Step.State, ESWGEngineState::Running);
	TestTrue(TEXT("accel sound on the way up"), Step.bPlayAccel);

	Step = FSWGVehicleEngine::Update(ESWGEngineState::Running, 0.04f);
	TestEqual(TEXT("hysteresis keeps running between the thresholds"), Step.State, ESWGEngineState::Running);

	Step = FSWGVehicleEngine::Update(ESWGEngineState::Running, 0.f);
	TestEqual(TEXT("stopping returns to idle"), Step.State, ESWGEngineState::Idle);
	TestTrue(TEXT("decel sound on the way down"), Step.bPlayDecel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGVehicleClientDataTest, "SWGEmu.Vehicle.ClientData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGVehicleClientDataTest::RunTest(const FString& Parameters)
{
	USWGTreSubsystem* Tre = LoadVehicleTre(*this);
	if (!Tre)
	{
		return false;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	FSWGClientDataFile Av21;
	if (TestTrue(TEXT("av21 client data parses"), FSWGClientDataFileReader::Read(Tre->CreateIffReader(TEXT("clientdata/vehicle/landspeeder_av21.iff")), Av21)))
	{
		if (TestEqual(TEXT("four thrust levels"), Av21.Thrusts.Num(), 4))
		{
			TestEqual(TEXT("level 0"), Av21.Thrusts[0].Level, 0.f);
			TestEqual(TEXT("level 1"), Av21.Thrusts[1].Level, 0.5f);
			TestEqual(TEXT("level 2"), Av21.Thrusts[2].Level, 0.75f);
			TestEqual(TEXT("level 3"), Av21.Thrusts[3].Level, 1.f);
			TestEqual(TEXT("sound hardpoint"), Av21.Thrusts[0].SoundHardpoint, FString(TEXT("engine_sound_0")));
			TestEqual(TEXT("idle"), Av21.Thrusts[0].IdleSound, FString(TEXT("sound/veh_av21speeder_idle_lp.snd")));
			TestEqual(TEXT("accel"), Av21.Thrusts[0].AccelSound, FString(TEXT("sound/veh_av21speeder_accel.snd")));
			TestEqual(TEXT("decel"), Av21.Thrusts[0].DecelSound, FString(TEXT("sound/veh_av21speeder_decel.snd")));
			TestEqual(TEXT("run"), Av21.Thrusts[0].RunSound, FString(TEXT("sound/veh_av21speeder_run_lp.snd")));
			TestEqual(TEXT("level 0 has no fire sounds"), Av21.Thrusts[0].FireSounds.Num(), 0);
			TestEqual(TEXT("level 1 fire sounds"), Av21.Thrusts[1].FireSounds.Num(), 4);
			TestEqual(TEXT("two thruster particles"), Av21.Thrusts[0].Thrusters.Num(), 2);
			TestEqual(TEXT("thruster hardpoint"), Av21.Thrusts[0].Thrusters[1].Hardpoint, FString(TEXT("engine_1")));
			// The full-thrust level names no vehicle sounds of its own.
			TestTrue(TEXT("level 3 sounds empty"), Av21.Thrusts[3].IdleSound.IsEmpty() && Av21.Thrusts[3].RunSound.IsEmpty());
		}
		if (TestEqual(TEXT("two ground effects"), Av21.GroundEffects.Num(), 2))
		{
			TestFalse(TEXT("dust trail is land"), Av21.GroundEffects[0].bWater);
			TestEqual(TEXT("dust trail particle"), Av21.GroundEffects[0].AppearancePath, FString(TEXT("appearance/pt_vehicle_dust_trail.prt")));
			TestTrue(TEXT("water trail is water"), Av21.GroundEffects[1].bWater);
			TestEqual(TEXT("water trail sound"), Av21.GroundEffects[1].Sound, FString(TEXT("sound/amb_river_large_lp.snd")));
		}
		const FSWGVehicleEngineSounds Sounds = FSWGVehicleEngineSounds::FromClientData(Av21);
		TestEqual(TEXT("engine run loop"), Sounds.Run, FString(TEXT("sound/veh_av21speeder_run_lp.snd")));
	}

	// Every vehicle file parses and every sound it names exists.
	FSWGDataTableData SaddleMap;
	TestTrue(TEXT("saddle appearance map reads"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/mount/saddle_appearance_map.iff")), SaddleMap));
	const TCHAR* Appearances[] = {
		TEXT("appearance/pv_landspeeder_av21.sat"), TEXT("appearance/pv_landspeeder_luke.sat"), TEXT("appearance/pv_speeder_bike.sat"),
		TEXT("appearance/pv_swoop_bike.sat"), TEXT("appearance/pv_jetpack_mandalorian.sat"), TEXT("appearance/pv_flash_speeder.sat") };
	for (const TCHAR* Appearance : Appearances)
	{
		const FString Path = FSWGVehicleEngine::FindClientDataPath(SaddleMap, Appearance);
		if (!TestFalse(*FString::Printf(TEXT("%s maps to client data"), Appearance), Path.IsEmpty()))
		{
			continue;
		}
		FSWGClientDataFile Data;
		if (TestTrue(*FString::Printf(TEXT("%s parses"), *Path), FSWGClientDataFileReader::Read(Tre->CreateIffReader(Path), Data)))
		{
			const FSWGVehicleEngineSounds Sounds = FSWGVehicleEngineSounds::FromClientData(Data);
			TestTrue(*FString::Printf(TEXT("%s has engine sounds"), *Path), Sounds.IsValid());
			for (const FString& Sound : { Sounds.Idle, Sounds.Accel, Sounds.Decel, Sounds.Run })
			{
				TestTrue(*FString::Printf(TEXT("%s: %s exists"), *Path, *Sound), Sound.IsEmpty() || Tre->FileExists(Sound));
			}
		}
	}
	TestEqual(TEXT("not a vehicle appearance"), FSWGVehicleEngine::FindClientDataPath(SaddleMap, TEXT("appearance/rancor.sat")), FString());
	return true;
}

#endif
