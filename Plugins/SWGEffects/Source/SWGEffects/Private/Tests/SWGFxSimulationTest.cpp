#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "Misc/ScopeExit.h"
#include "Simulation/SWGEffectSimulation.h"
#include "Subsystems/SWGTreSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Deterministic checks of the particle simulation: synthetic emitters for each rule, then three real retail files. */
namespace
{
	constexpr float StepSeconds = 1.f / 60.f;

	FSWGWaveform MakeConstant(float Value, float SpreadBelow = 0.f, float SpreadAbove = 0.f)
	{
		FSWGWaveform Curve;
		Curve.Points.Add({0.f, Value, SpreadBelow, SpreadAbove});
		Curve.Points.Add({1.f, Value, SpreadBelow, SpreadAbove});
		return Curve;
	}

	FSWGWaveform MakeRamp(float StartValue, float EndValue)
	{
		FSWGWaveform Curve;
		Curve.Points.Add({0.f, StartValue, 0.f, 0.f});
		Curve.Points.Add({1.f, EndValue, 0.f, 0.f});
		return Curve;
	}

	/** A quad emitter that emits nothing special: rate 10/s, lifetime 1 s, still, single run. */
	FSWGParticleEmitter MakeEmitter()
	{
		FSWGParticleEmitter Emitter;
		Emitter.Version = 12;
		Emitter.Kind = ESWGParticleKind::Quad;
		Emitter.Timing.bPresent = true;
		Emitter.Params.EmissionStyle = 1;
		Emitter.Params.RunTimeMin = Emitter.Params.RunTimeMax = 4.f;
		for (int32 Index = 0; Index < (int32)ESWGEmitterCurve::Count; ++Index)
		{
			Emitter.Curves[Index] = MakeConstant(0.f);
		}
		Emitter.Curves[(int32)ESWGEmitterCurve::Rate] = MakeConstant(10.f);
		Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(1.f);
		Emitter.Particle.Alpha = MakeConstant(1.f);
		Emitter.Particle.Speed = MakeConstant(1.f);
		Emitter.Particle.Color.Points.Add({0.f, FLinearColor::White});
		Emitter.ShapeCurves = {MakeConstant(0.f), MakeConstant(0.1f), MakeConstant(0.1f)};
		Emitter.Texture.FrameCount = 1;
		return Emitter;
	}

	TSharedRef<FSWGParticleEffect> MakeEffect(const FSWGParticleEmitter& Emitter)
	{
		TSharedRef<FSWGParticleEffect> Effect = MakeShared<FSWGParticleEffect>();
		Effect->Version = 1;
		Effect->Timing.bPresent = true;
		FSWGEmitterGroup& Group = Effect->Groups.AddDefaulted_GetRef();
		Group.Timing.bPresent = true;
		Group.Emitters.Add(Emitter);
		return Effect;
	}

	void RunFor(FSWGEffectSimulation& Simulation, float Seconds, const FSWGEffectFrame& Frame = FSWGEffectFrame())
	{
		const int32 Steps = FMath::RoundToInt(Seconds / StepSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Simulation.Update(StepSeconds, Frame);
		}
	}

	FSWGEffectSimSettings MakeSettings(uint32 Seed = 7)
	{
		FSWGEffectSimSettings Settings;
		Settings.Seed = Seed;
		return Settings;
	}

	USWGTreSubsystem* LoadTreSubsystem()
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
		USWGTreSubsystem* Tre = Outer ? NewObject<USWGTreSubsystem>(Outer) : nullptr;
		if (!Tre)
		{
			return nullptr;
		}
		Tre->AddToRoot();
		Tre->LoadConfig();
		if (!Tre->LoadArchives())
		{
			Tre->RemoveFromRoot();
			return nullptr;
		}
		return Tre;
	}

	bool IsInsideCage(const FVector& Position, const FSWGEmitterSimulation& Flocking)
	{
		const FVector Offset = (Position - Flocking.GetCageCenter()).GetAbs();
		const FVector Limit = Flocking.GetCageHalfExtent() + FVector(1e-3);
		return Offset.X <= Limit.X && Offset.Y <= Limit.Y && Offset.Z <= Limit.Z;
	}

	bool LoadEffect(const USWGTreSubsystem& Tre, const TCHAR* Path, FSWGParticleEffect& OutEffect)
	{
		const FSWGIffReader Reader = Tre.CreateIffReader(Path);
		return Reader.IsValid() && FSWGParticleEffectReader::Read(Reader, OutEffect);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWGFxSimulationTest,
	"SWGEmu.Fx.Simulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGFxSimulationTest::RunTest(const FString& Parameters)
{
	// Curve sampling: linear between keys, random spread bounds.
	{
		FSWGWaveform Curve;
		Curve.SampleMode = 0;
		Curve.Points.Add({0.f, 1.f, 0.5f, 0.25f});
		Curve.Points.Add({1.f, 3.f, 0.5f, 0.25f});
		TestEqual(TEXT("curve midpoint"), Curve.EvaluateValue(0.5f), 2.f, 0.0001f);
		TestEqual(TEXT("curve spread low"), Curve.Sample(0.5f, 0.f), 1.5f, 0.0001f);
		TestEqual(TEXT("curve spread high"), Curve.Sample(0.5f, 1.f), 2.25f, 0.0001f);

		Curve.SampleMode = 1;
		for (float Random01 : {0.f, 0.3f, 0.9f})
		{
			const float Rerolled = Curve.Sample(0.5f, Random01);
			TestTrue(TEXT("rerolled sample stays inside the spread"), Rerolled >= 1.5f - 0.0001f && Rerolled <= 2.25f + 0.0001f);
		}
		Curve.SampleMode = 2;
		TestEqual(TEXT("sample mode 2 has no spread"), Curve.Sample(0.5f, 1.f), 2.f, 0.0001f);
		Curve.SampleMode = 0;
		Curve.bHasClamp = true;
		Curve.ClampMin = 0.f;
		Curve.ClampMax = 2.f;
		TestEqual(TEXT("clamp limits the result"), Curve.Sample(0.5f, 1.f), 2.f, 0.0001f);
	}

	// Emission shapes: unit vectors on each shape's outline.
	{
		for (const int32 Shape : {0, 1, 2, 3, 4, 5})
		{
			FSWGParticleEmitter Emitter = MakeEmitter();
			Emitter.Params.EmissionStyle = 0;
			Emitter.Params.Shape = Shape;
			Emitter.Params.bBurst = true;
			Emitter.Params.CountMin = Emitter.Params.CountMax = 40;
			Emitter.Curves[(int32)ESWGEmitterCurve::ShapeSize] = MakeConstant(2.f);
			Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(5.f);
			FSWGEffectSimulation Simulation;
			Simulation.Initialize(MakeEffect(Emitter), MakeSettings());
			Simulation.Update(StepSeconds, FSWGEffectFrame());
			TestEqual(*FString::Printf(TEXT("shape %d burst count"), Shape), Simulation.GetLiveParticleCount(), 40);
			bool bOnOutline = true;
			for (const FSWGSimParticle& Particle : Simulation.GetEmitter(0).GetParticles())
			{
				const FVector Point = Particle.Position / 2.0;
				switch (Shape)
				{
				case 0: bOnOutline &= FMath::Abs(Point.Y) < 0.01 && FMath::IsNearlyEqual(Point.Size(), 1.0, 0.01); break;
				case 1: bOnOutline &= FMath::IsNearlyEqual(Point.Size(), 1.0, 0.01); break;
				case 2: bOnOutline &= FMath::Abs(Point.Y) < 0.01 && FMath::Max(FMath::Abs(Point.X), FMath::Abs(Point.Z)) > 0.95; break;
				case 3: bOnOutline &= Point.GetAbsMax() > 0.95; break;
				case 4: bOnOutline &= FMath::Abs(Point.X) < 0.01 && FMath::Abs(Point.Y) < 0.01 && FMath::Abs(Point.Z) > 0.95; break;
				default: bOnOutline &= FMath::Abs(Point.Y) < 0.01 && FMath::IsNearlyEqual(Point.GetAbsMax(), 1.0, 0.01) && FMath::IsNearlyZero(Point.X * Point.Z, 0.01); break;
				}
			}
			TestTrue(*FString::Printf(TEXT("shape %d spawns on its outline"), Shape), bOnOutline);
		}
	}

	// Cone emission: every particle leaves at exactly the spread angle off +Y.
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Params.bBurst = true;
		Emitter.Params.CountMin = Emitter.Params.CountMax = 30;
		Emitter.Curves[(int32)ESWGEmitterCurve::Spread] = MakeConstant(30.f);
		Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(5.f);
		FSWGEffectSimulation Simulation;
		Simulation.Initialize(MakeEffect(Emitter), MakeSettings());
		Simulation.Update(StepSeconds, FSWGEffectFrame());
		bool bExactAngle = Simulation.GetLiveParticleCount() == 30;
		for (const FSWGSimParticle& Particle : Simulation.GetEmitter(0).GetParticles())
		{
			bExactAngle &= FMath::IsNearlyEqual(Particle.Direction.Y, FMath::Cos(FMath::DegreesToRadians(30.0)), 0.001);
		}
		TestTrue(TEXT("cone: fixed polar angle"), bExactAngle);
	}

	// A run lasts the emitter's run time; the loop delay is the gap before the next run.
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Params.RunTimeMin = Emitter.Params.RunTimeMax = 1.f;
		Emitter.Params.bIgnoreLiveParticles = true;
		Emitter.Timing.LoopDelayMin = Emitter.Timing.LoopDelayMax = 1.f;
		Emitter.Timing.LoopCountMin = Emitter.Timing.LoopCountMax = 1;
		Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(0.1f);
		FSWGEffectSimulation Simulation;
		Simulation.Initialize(MakeEffect(Emitter), MakeSettings());
		RunFor(Simulation, 0.9f);
		const int32 FirstRun = Simulation.GetEmitter(0).GetRunsStarted();
		RunFor(Simulation, 0.7f);
		TestEqual(TEXT("first run only, then the gap"), FirstRun, 1);
		TestEqual(TEXT("still in the gap at 1.6 s"), Simulation.GetLiveParticleCount(), 0);
		RunFor(Simulation, 0.8f);
		TestEqual(TEXT("second run after the gap"), Simulation.GetEmitter(0).GetRunsStarted(), 2);
		RunFor(Simulation, 2.f);
		TestTrue(TEXT("two runs, then finished"), Simulation.IsFinished());
	}

	// A looping run waits for its particles unless told not to.
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Params.RunTimeMin = Emitter.Params.RunTimeMax = 0.5f;
		Emitter.Timing.LoopCountMin = Emitter.Timing.LoopCountMax = -1;
		Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(2.f);
		FSWGEffectSimulation Waiting;
		Waiting.Initialize(MakeEffect(Emitter), MakeSettings());
		RunFor(Waiting, 2.0f);
		TestEqual(TEXT("waits for particles to die before looping"), Waiting.GetEmitter(0).GetRunsStarted(), 1);
		Emitter.Params.bIgnoreLiveParticles = true;
		FSWGEffectSimulation Eager;
		Eager.Initialize(MakeEffect(Emitter), MakeSettings());
		RunFor(Eager, 2.0f);
		TestTrue(TEXT("loops straight away when allowed"), Eager.GetEmitter(0).GetRunsStarted() >= 3);
	}

	// Spawn rate: 10/s for 2 s.
	{
		FSWGEffectSimulation Simulation;
		Simulation.Initialize(MakeEffect(MakeEmitter()), MakeSettings());
		RunFor(Simulation, 0.5f);
		TestTrue(TEXT("rate: 5 particles after 0.5 s"), FMath::Abs(Simulation.GetLiveParticleCount() - 5) <= 1);
	}
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(100.f);
		FSWGEffectSimulation Simulation;
		Simulation.Initialize(MakeEffect(Emitter), MakeSettings());
		RunFor(Simulation, 2.f);
		TestTrue(TEXT("rate: 20 particles after 2 s"), FMath::Abs(Simulation.GetLiveParticleCount() - 20) <= 1);
	}

	// The rate curve runs over the run time: 0 -> 20/s across 2 s emits about 20.
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Params.RunTimeMin = Emitter.Params.RunTimeMax = 2.f;
		Emitter.Curves[(int32)ESWGEmitterCurve::Rate] = MakeRamp(0.f, 20.f);
		Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(100.f);
		FSWGEffectSimulation Simulation;
		Simulation.Initialize(MakeEffect(Emitter), MakeSettings());
		RunFor(Simulation, 1.99f);
		TestTrue(TEXT("rate curve integrates over the run"), FMath::Abs(Simulation.GetLiveParticleCount() - 20) <= 3);
		TestTrue(TEXT("a one-run emitter finishes emitting"), (RunFor(Simulation, 0.1f), Simulation.GetEmitter(0).IsEmissionFinished()));
	}

	// Start delay.
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Timing.StartDelayMin = Emitter.Timing.StartDelayMax = 0.5f;
		FSWGEffectSimulation Simulation;
		Simulation.Initialize(MakeEffect(Emitter), MakeSettings());
		RunFor(Simulation, 0.4f);
		TestEqual(TEXT("nothing before the start delay"), Simulation.GetLiveParticleCount(), 0);
		RunFor(Simulation, 0.3f);
		TestTrue(TEXT("particles after the start delay"), Simulation.GetLiveParticleCount() > 0);
	}

	// One-shot burst versus looping burst.
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Params.bBurst = true;
		Emitter.Params.CountMin = Emitter.Params.CountMax = 5;
		Emitter.Params.RunTimeMin = Emitter.Params.RunTimeMax = 1.2f;
		FSWGEffectSimulation OneShot;
		OneShot.Initialize(MakeEffect(Emitter), MakeSettings());
		OneShot.Update(StepSeconds, FSWGEffectFrame());
		TestEqual(TEXT("burst count"), OneShot.GetLiveParticleCount(), 5);
		TestFalse(TEXT("not finished while particles live"), OneShot.IsFinished());
		RunFor(OneShot, 1.3f);
		TestEqual(TEXT("one-shot burst: particles die"), OneShot.GetLiveParticleCount(), 0);
		TestTrue(TEXT("one-shot finishes by itself"), OneShot.IsFinished());

		Emitter.Timing.LoopCountMin = Emitter.Timing.LoopCountMax = -1;
		FSWGEffectSimulation Looping;
		Looping.Initialize(MakeEffect(Emitter), MakeSettings());
		RunFor(Looping, 3.5f);
		TestTrue(TEXT("looping emitter runs again"), Looping.GetEmitter(0).GetRunsStarted() >= 3);
		TestFalse(TEXT("loop does not finish on its own"), Looping.IsFinished());
		Looping.Stop(false);
		RunFor(Looping, 1.3f);
		TestTrue(TEXT("stopped loop drains and finishes"), Looping.IsFinished());

		FSWGEffectSimulation Immediate;
		Immediate.Initialize(MakeEffect(Emitter), MakeSettings());
		RunFor(Immediate, 0.5f);
		Immediate.Stop(true);
		TestTrue(TEXT("immediate stop clears everything"), Immediate.IsFinished() && Immediate.GetLiveParticleCount() == 0);
	}

	// Emitter rotation: a quarter turn about X sends +Y emission along +Z (bolts).
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Params.bBurst = true;
		Emitter.Params.CountMin = Emitter.Params.CountMax = 1;
		Emitter.Curves[(int32)ESWGEmitterCurve::RotateX] = MakeConstant(0.25f);
		Emitter.Curves[(int32)ESWGEmitterCurve::Speed] = MakeConstant(2.f);
		FSWGEffectSimulation Simulation;
		Simulation.Initialize(MakeEffect(Emitter), MakeSettings());
		RunFor(Simulation, 0.5f);
		TArray<FSWGParticleRenderData> Data;
		Simulation.GetEmitter(0).GatherRenderData(FSWGEffectFrame(), Data);
		TestEqual(TEXT("bolt particle count"), Data.Num(), 1);
		if (Data.Num() == 1)
		{
			TestTrue(TEXT("a quarter turn about X fires along +Z"), FMath::IsNearlyEqual((float)Data[0].WorldPosition.Z, 1.f, 0.05f) && FMath::Abs(Data[0].WorldPosition.Y) < 0.05);
		}
	}

	// Gravity pulls along -Y in multiples of 9.8 m/s^2; wind moves a particle by what its resistance lets through.
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Params.bBurst = true;
		Emitter.Params.CountMin = Emitter.Params.CountMax = 1;
		Emitter.Params.ParamFloats[4] = 0.5f;
		Emitter.Curves[(int32)ESWGEmitterCurve::Gravity] = MakeConstant(2.f / 9.8f);
		Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(5.f);
		FSWGEffectFrame Frame;
		Frame.Wind = FVector(2.0, 0.0, 0.0);
		FSWGEffectSimulation Simulation;
		Simulation.Initialize(MakeEffect(Emitter), MakeSettings());
		RunFor(Simulation, 1.f, Frame);
		TArray<FSWGParticleRenderData> Data;
		Simulation.GetEmitter(0).GatherRenderData(Frame, Data);
		if (TestEqual(TEXT("gravity particle count"), Data.Num(), 1))
		{
			TestTrue(TEXT("gravity: y = -g t^2 / 2"), FMath::IsNearlyEqual((float)Data[0].WorldPosition.Y, -1.f, 0.1f));
			TestTrue(TEXT("wind: x = wind x resistance x t"), FMath::IsNearlyEqual((float)Data[0].WorldPosition.X, 1.f, 0.1f));
		}
	}

	// World-space particles stay put when the effect moves; local-space ones ride along.
	{
		for (const bool bWorld : {true, false})
		{
			FSWGParticleEmitter Emitter = MakeEmitter();
			Emitter.Params.bBurst = true;
			Emitter.Params.CountMin = Emitter.Params.CountMax = 1;
			Emitter.Params.bLocalSpace = !bWorld;
			Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(5.f);
			FSWGEffectFrame Frame;
			FSWGEffectSimulation Simulation;
			Simulation.Initialize(MakeEffect(Emitter), MakeSettings());
			RunFor(Simulation, 0.1f, Frame);
			Frame.Location = FVector(10.0, 0.0, 0.0);
			RunFor(Simulation, 0.1f, Frame);
			TArray<FSWGParticleRenderData> Data;
			Simulation.GetEmitter(0).GatherRenderData(Frame, Data);
			if (TestEqual(TEXT("space particle count"), Data.Num(), 1))
			{
				TestTrue(bWorld ? TEXT("world-space particle stays") : TEXT("local-space particle follows"), bWorld ? Data[0].WorldPosition.X < 1.0 : Data[0].WorldPosition.X > 9.0);
			}
		}
	}

	// Generate-by-distance: nothing while still, one per spacing when moving.
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Params.bGenerateByDistance = true;
		Emitter.Curves[(int32)ESWGEmitterCurve::Rate] = MakeConstant(0.5f);
		Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(100.f);
		FSWGEffectSimulation Simulation;
		Simulation.Initialize(MakeEffect(Emitter), MakeSettings());
		FSWGEffectFrame Frame;
		RunFor(Simulation, 0.5f, Frame);
		TestEqual(TEXT("distance emitter idle while still"), Simulation.GetLiveParticleCount(), 0);
		for (int32 Step = 0; Step < 60; ++Step)
		{
			Frame.Location.X += 5.0 / 60.0;
			Simulation.Update(StepSeconds, Frame);
		}
		TestTrue(TEXT("distance emitter: 5 m at 0.5 m spacing"), FMath::Abs(Simulation.GetLiveParticleCount() - 10) <= 1);
	}

	// Flipbook frames.
	{
		FSWGParticleTexture Fire;
		Fire.FrameCount = 16;
		Fire.FrameStart = 0;
		Fire.FrameEnd = 15;
		Fire.GridColumns = 4;
		Fire.FramesPerSecond = 16.f;
		TestEqual(TEXT("flipbook frame at 0"), FSWGEmitterSimulation::ComputeFlipbookFrame(Fire, 0.f, 0.f), 0);
		TestEqual(TEXT("flipbook frame at 0.5 s"), FSWGEmitterSimulation::ComputeFlipbookFrame(Fire, 0.5f, 0.5f), 8);
		TestEqual(TEXT("flipbook wraps at 1 s"), FSWGEmitterSimulation::ComputeFlipbookFrame(Fire, 1.01f, 0.5f), 0);
		Fire.FramesPerSecond = -1.f;
		TestEqual(TEXT("play-once strip at half life"), FSWGEmitterSimulation::ComputeFlipbookFrame(Fire, 99.f, 0.5f), 8);
		TestEqual(TEXT("play-once strip clamps at the end"), FSWGEmitterSimulation::ComputeFlipbookFrame(Fire, 99.f, 1.f), 15);

		FSWGParticleTexture Wings;
		Wings.FrameStart = 2;
		Wings.FrameEnd = 3;
		Wings.FramesPerSecond = 60.f;
		TestEqual(TEXT("sub-range starts at its first frame"), FSWGEmitterSimulation::ComputeFlipbookFrame(Wings, 0.f, 0.f), 2);
		TestEqual(TEXT("sub-range advances"), FSWGEmitterSimulation::ComputeFlipbookFrame(Wings, 1.f / 60.f + 0.001f, 0.f), 3);
	}

	// Child effects: a trail child at birth, a death child at the end of life.
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Params.bBurst = true;
		Emitter.Params.CountMin = Emitter.Params.CountMax = 3;
		Emitter.Particle.bHasChild = true;
		Emitter.Particle.Child.Path = TEXT("appearance/pt_child.prt");
		Emitter.Particle.Child.Mode = 2;
		FSWGEffectSimulation Trail;
		Trail.Initialize(MakeEffect(Emitter), MakeSettings());
		Trail.Update(StepSeconds, FSWGEffectFrame());
		const TArray<FSWGChildSpawnRequest> TrailRequests = Trail.ConsumeChildRequests();
		TestEqual(TEXT("one trail child per particle at birth"), TrailRequests.Num(), 3);
		TestTrue(TEXT("trail children follow"), !TrailRequests.IsEmpty() && TrailRequests[0].bFollowParticle && TrailRequests[0].EffectPath == TEXT("appearance/pt_child.prt"));

		Emitter.Particle.Child.Mode = 1;
		Emitter.Particle.Child.ValueA = Emitter.Particle.Child.ValueB = 1.f;
		FSWGEffectSimulation Burst;
		Burst.Initialize(MakeEffect(Emitter), MakeSettings());
		Burst.Update(StepSeconds, FSWGEffectFrame());
		TestEqual(TEXT("no death child while alive"), Burst.ConsumeChildRequests().Num(), 0);
		RunFor(Burst, 1.1f);
		TestEqual(TEXT("death children when the particles end"), Burst.ConsumeChildRequests().Num(), 3);
	}

	// Determinism.
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Curves[(int32)ESWGEmitterCurve::Spread] = MakeConstant(30.f);
		Emitter.Curves[(int32)ESWGEmitterCurve::Speed] = MakeConstant(1.f, 0.5f, 0.5f);
		Emitter.Curves[(int32)ESWGEmitterCurve::ShapeSize] = MakeConstant(0.2f);
		auto Snapshot = [&Emitter](uint32 Seed)
		{
			FSWGEffectSimulation Simulation;
			Simulation.Initialize(MakeEffect(Emitter), MakeSettings(Seed));
			RunFor(Simulation, 0.8f);
			TArray<FSWGParticleRenderData> Data;
			Simulation.GetEmitter(0).GatherRenderData(FSWGEffectFrame(), Data);
			return Data;
		};
		const TArray<FSWGParticleRenderData> First = Snapshot(42);
		const TArray<FSWGParticleRenderData> Second = Snapshot(42);
		const TArray<FSWGParticleRenderData> Other = Snapshot(43);
		bool bIdentical = First.Num() == Second.Num() && First.Num() > 0;
		for (int32 Index = 0; bIdentical && Index < First.Num(); ++Index)
		{
			bIdentical = First[Index].WorldPosition.Equals(Second[Index].WorldPosition, 0.0);
		}
		TestTrue(TEXT("same seed, same particles"), bIdentical);
		TestTrue(TEXT("different seed, different particles"), Other.IsEmpty() || First.IsEmpty() || !First[0].WorldPosition.Equals(Other[0].WorldPosition, 1e-6));
	}

	// Flocking stays inside its cage.
	{
		FSWGParticleEmitter Emitter = MakeEmitter();
		Emitter.Params.FlockMode = 1;
		Emitter.Params.MaxParticles = 24.f;
		const float Flock[7] = {5.f, 0.3f, 0.5f, 0.4f, 3.f, 2.f, 0.f};
		FMemory::Memcpy(Emitter.Params.FlockParams, Flock, sizeof(Flock));
		Emitter.Curves[(int32)ESWGEmitterCurve::Rate] = MakeConstant(8.f);
		Emitter.Curves[(int32)ESWGEmitterCurve::Speed] = MakeConstant(1.5f, 0.5f, 0.5f);
		Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(20.f);
		FSWGEffectSimulation Simulation;
		Simulation.Initialize(MakeEffect(Emitter), MakeSettings());
		bool bInside = true;
		int32 MostParticles = 0;
		for (int32 Step = 0; Step < 30 * 40; ++Step)
		{
			Simulation.Update(1.f / 30.f, FSWGEffectFrame());
			const FSWGEmitterSimulation& Flocking = Simulation.GetEmitter(0);
			MostParticles = FMath::Max(MostParticles, Flocking.GetParticleCount());
			for (const FSWGSimParticle& Particle : Flocking.GetParticles())
			{
				bInside &= IsInsideCage(Particle.Position, Flocking);
			}
		}
		TestTrue(TEXT("synthetic flock stays in the cage"), bInside);
		TestTrue(TEXT("flock builds up to its particle cap"), MostParticles >= 20 && MostParticles <= 24);
	}

	// Retail files.
	USWGTreSubsystem* Tre = LoadTreSubsystem();
	if (!Tre)
	{
		AddWarning(TEXT("no .tre archives loaded - skipped the retail file checks"));
		return true;
	}
	ON_SCOPE_EXIT{ Tre->RemoveFromRoot(); };

	// pt_bolt_red: four emitters, one particle each, 20 s life.
	{
		TSharedRef<FSWGParticleEffect> Bolt = MakeShared<FSWGParticleEffect>();
		if (TestTrue(TEXT("pt_bolt_red parses"), LoadEffect(*Tre, TEXT("appearance/pt_bolt_red.prt"), *Bolt)))
		{
			FSWGEffectSimulation Simulation;
			Simulation.Initialize(Bolt, MakeSettings());
			TestEqual(TEXT("bolt emitter count"), Simulation.GetEmitterCount(), 4);
			int32 MostPerEmitter = 0;
			for (int32 Step = 0; Step < 60 * 45; ++Step)
			{
				Simulation.Update(StepSeconds, FSWGEffectFrame());
				for (int32 EmitterIndex = 0; EmitterIndex < Simulation.GetEmitterCount(); ++EmitterIndex)
				{
					MostPerEmitter = FMath::Max(MostPerEmitter, Simulation.GetEmitter(EmitterIndex).GetParticleCount());
				}
				if (Step == 60)
				{
					TestEqual(TEXT("bolt shows four particles at 1 s"), Simulation.GetLiveParticleCount(), 4);
				}
			}
			TestEqual(TEXT("bolt never exceeds one particle per emitter"), MostPerEmitter, 1);
			TestEqual(TEXT("bolt still shows four particles after respawning"), Simulation.GetLiveParticleCount(), 4);
			TestFalse(TEXT("bolt effect loops until stopped"), Simulation.IsFinished());
			Simulation.Stop(true);
			TestTrue(TEXT("bolt stops immediately"), Simulation.IsFinished());
		}
	}

	// pt_campfire_s01: three emitters, fire is a 16-frame 4x4 flipbook.
	{
		TSharedRef<FSWGParticleEffect> Campfire = MakeShared<FSWGParticleEffect>();
		if (TestTrue(TEXT("pt_campfire_s01 parses"), LoadEffect(*Tre, TEXT("appearance/pt_campfire_s01.prt"), *Campfire)))
		{
			FSWGEffectSimulation Simulation;
			Simulation.Initialize(Campfire, MakeSettings());
			TestEqual(TEXT("campfire emitter count"), Simulation.GetEmitterCount(), 3);
			RunFor(Simulation, 0.1f);
			TestEqual(TEXT("smoke waits for its start delay"), Simulation.GetEmitter(0).GetParticleCount(), 0);
			RunFor(Simulation, 3.f);
			for (int32 EmitterIndex = 0; EmitterIndex < 3; ++EmitterIndex)
			{
				TestTrue(*FString::Printf(TEXT("campfire emitter %d is emitting"), EmitterIndex), Simulation.GetEmitter(EmitterIndex).GetParticleCount() > 0);
			}

			TSet<int32> Frames;
			for (int32 Step = 0; Step < 120; ++Step)
			{
				Simulation.Update(StepSeconds, FSWGEffectFrame());
				TArray<FSWGParticleRenderData> Data;
				Simulation.GetEmitter(2).GatherRenderData(FSWGEffectFrame(), Data);
				for (const FSWGParticleRenderData& Particle : Data)
				{
					Frames.Add(Particle.Frame);
				}
			}
			bool bFramesInRange = !Frames.IsEmpty();
			for (const int32 Frame : Frames)
			{
				bFramesInRange &= Frame >= 0 && Frame <= 15;
			}
			TestTrue(TEXT("fire frames stay inside the 16-frame strip"), bFramesInRange);
			TestTrue(TEXT("fire cycles through several frames"), Frames.Num() >= 8);
		}
	}

	// pt_buzzing_insects_small: flocking inside its cage.
	{
		TSharedRef<FSWGParticleEffect> Insects = MakeShared<FSWGParticleEffect>();
		if (TestTrue(TEXT("pt_buzzing_insects_small parses"), LoadEffect(*Tre, TEXT("appearance/pt_buzzing_insects_small.prt"), *Insects)))
		{
			FSWGEffectSimulation Simulation;
			Simulation.Initialize(Insects, MakeSettings());
			bool bInside = true;
			int32 MostParticles = 0;
			for (int32 Step = 0; Step < 30 * 60; ++Step)
			{
				Simulation.Update(1.f / 30.f, FSWGEffectFrame());
				for (int32 EmitterIndex = 0; EmitterIndex < Simulation.GetEmitterCount(); ++EmitterIndex)
				{
					const FSWGEmitterSimulation& Flocking = Simulation.GetEmitter(EmitterIndex);
					MostParticles = FMath::Max(MostParticles, Flocking.GetParticleCount());
					for (const FSWGSimParticle& Particle : Flocking.GetParticles())
					{
						bInside &= IsInsideCage(Particle.Position, Flocking);
					}
				}
			}
			TestTrue(TEXT("insects are flocking emitters"), Simulation.GetEmitter(0).IsFlocking());
			TestTrue(TEXT("insects stay in their cage"), bInside);
			TestTrue(TEXT("insect swarm builds up and respects its particle cap"), MostParticles >= 1 && MostParticles <= Simulation.GetEmitter(0).GetParticleCap());
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGFxTerrainTest, "SWGEmu.Effects.Simulation.Terrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSWGFxTerrainTest::RunTest(const FString& Parameters)
{
	FSWGParticleEmitter Emitter = MakeEmitter();
	Emitter.Params.bBurst = true;
	Emitter.Params.CountMin = Emitter.Params.CountMax = 1;
	Emitter.Params.bTerrainCollide = true;
	Emitter.Params.GroundOffset = 0.25f;
	Emitter.Curves[(int32)ESWGEmitterCurve::TranslateY] = MakeConstant(1.f);
	Emitter.Curves[(int32)ESWGEmitterCurve::Spread] = MakeConstant(180.f);
	Emitter.Curves[(int32)ESWGEmitterCurve::Speed] = MakeConstant(2.f);
	Emitter.Curves[(int32)ESWGEmitterCurve::Lifetime] = MakeConstant(3.f);
	Emitter.Particle.bHasChild = true;
	Emitter.Particle.Child.Path = TEXT("appearance/test_splash.prt");
	Emitter.Particle.Child.Mode = 3;
	FSWGEffectSimSettings Settings = MakeSettings();
	Settings.TerrainHeight = [](const FVector&, float& Height) { Height = 0.f; return true; };
	FSWGEffectFrame Frame;
	FSWGEffectSimulation Simulation;
	Simulation.Initialize(MakeEffect(Emitter), Settings);
	Simulation.Update(0.f, Frame);
	TestTrue(TEXT("terrain child does not spawn at birth"), Simulation.ConsumeChildRequests().IsEmpty());
	Simulation.Update(0.5f, Frame);
	const TArray<FSWGSimParticle>& Particles = Simulation.GetEmitter(0).GetParticles();
	TestEqual(TEXT("bounce keeps particle alive"), Particles.Num(), 1);
	if (!Particles.IsEmpty()) TestEqual(TEXT("contact offset"), Particles[0].Position.Y, 0.25);
	const TArray<FSWGChildSpawnRequest> Children = Simulation.ConsumeChildRequests();
	TestEqual(TEXT("terrain hit spawns one child"), Children.Num(), 1);
	if (!Children.IsEmpty()) TestEqual(TEXT("child at contact"), Children[0].WorldPosition.Y, 0.25);
	Simulation.Update(0.1f, Frame);
	if (!Particles.IsEmpty()) TestTrue(TEXT("vertical bounce moves away from ground"), Particles[0].Position.Y > 0.25);
	TestTrue(TEXT("terrain child only fires once"), Simulation.ConsumeChildRequests().IsEmpty());

	Emitter.Params.bKillOnTerrainHit = true;
	Simulation.Initialize(MakeEffect(Emitter), Settings);
	Simulation.Update(0.f, Frame);
	Simulation.Update(0.5f, Frame);
	TestEqual(TEXT("kill on hit removes particle"), Simulation.GetEmitter(0).GetParticleCount(), 0);
	TestEqual(TEXT("killed particle still produces splash"), Simulation.ConsumeChildRequests().Num(), 1);
	Settings.TerrainHeight = [](const FVector&, float&) { return false; };
	Simulation.Initialize(MakeEffect(Emitter), Settings);
	Simulation.Update(0.f, Frame);
	Simulation.Update(0.5f, Frame);
	TestEqual(TEXT("missing terrain does not kill"), Simulation.GetEmitter(0).GetParticleCount(), 1);
	TestTrue(TEXT("missing terrain produces no splash"), Simulation.ConsumeChildRequests().IsEmpty());

	Emitter.Params.bLocalSpace = true;
	Emitter.Params.bTerrainCollide = false;
	Emitter.Params.bSnapToTerrain = true;
	Emitter.Params.bTerrainZeroVelocity = true;
	Emitter.Params.TerrainHeightOffset = 0.5f;
	Settings.TerrainHeight = [](const FVector&, float& Height) { Height = 5.f; return true; };
	Frame.Location = FVector(10, 20, 30);
	Frame.Rotation = FQuat(FVector::ZAxisVector, UE_PI / 2);
	Frame.Scale = 2.f;
	Simulation.Initialize(MakeEffect(Emitter), Settings);
	Simulation.Update(0.f, Frame);
	const FSWGEmitterSimulation& Local = Simulation.GetEmitter(0);
	TestEqual(TEXT("local snap has a particle"), Local.GetParticleCount(), 1);
	if (Local.GetParticleCount() == 1)
	{
		TestTrue(TEXT("rotated/scaled local emitter snaps in world space"), FMath::IsNearlyEqual(Local.ToWorldPosition(Local.GetParticles()[0], Frame).Y, 6.0));
		TestEqual(TEXT("snap zeroes spawn velocity"), Local.GetParticles()[0].BaseSpeed, 0.f);
	}
	return true;
}

#endif
