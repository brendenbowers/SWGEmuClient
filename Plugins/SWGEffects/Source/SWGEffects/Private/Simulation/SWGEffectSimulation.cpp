#include "Simulation/SWGEffectSimulation.h"

namespace
{
	constexpr float TurnsToRadians = UE_TWO_PI;

	/** Emitter/mesh rotation curves are in turns; X is applied first, then Y, then Z (order inferred). */
	FQuat MakeTurnsRotation(float TurnsX, float TurnsY, float TurnsZ)
	{
		return FQuat(FVector::ZAxisVector, TurnsZ * TurnsToRadians)
			* FQuat(FVector::YAxisVector, TurnsY * TurnsToRadians)
			* FQuat(FVector::XAxisVector, TurnsX * TurnsToRadians);
	}

	/** The sim's "up": gravity pulls along -Y. */
	const FVector SimUp(0.0, 1.0, 0.0);

	/** Retail gravity curve unit: multiples of this (0x7a4840). */
	constexpr float GravityMetresPerSecondSquared = 9.8f;

	FSWGEffectFrame LerpFrame(const FSWGEffectFrame& From, const FSWGEffectFrame& To, float Alpha)
	{
		FSWGEffectFrame Result = To;
		Result.Location = FMath::Lerp(From.Location, To.Location, (double)Alpha);
		Result.Rotation = FQuat::Slerp(From.Rotation, To.Rotation, Alpha);
		return Result;
	}
}

void FSWGEmitterSimulation::Initialize(const FSWGParticleEmitter& InEmitter, const FSWGEffectTiming& EffectTiming, const FSWGEffectTiming& GroupTiming,
	const FSWGEffectSimSettings& InSettings, uint32 EmitterSeed, int32 InEmitterIndex)
{
	Emitter = &InEmitter;
	Settings = InSettings;
	EmitterIndex = InEmitterIndex;
	Random.Initialize((int32)EmitterSeed);

	const FSWGEmitterParams& Params = Emitter->Params;
	// Flocks keep their cage in effect-local space, whatever the flag says.
	bWorldSpace = Settings.bForceWorldSpace || (!Params.bLocalSpace && !Params.IsFlocking());

	StartDelaySeconds = 0.f;
	for (const FSWGEffectTiming* Level : {&EffectTiming, &GroupTiming, &Emitter->Timing})
	{
		if (Level->bPresent)
		{
			StartDelaySeconds += SampleRange(Level->StartDelayMin, Level->StartDelayMax);
		}
	}
	DelayRemaining = StartDelaySeconds;

	// The emitter's own PTIM loops its runs; a group that loops (and an emitter that does not) repeats them instead.
	const FSWGEffectTiming* LoopSource = &Emitter->Timing;
	for (const FSWGEffectTiming* Level : {&Emitter->Timing, &GroupTiming})
	{
		if (Level->bPresent && (Level->LoopDelayMax > 0.f || Level->LoopCountMin != 0 || Level->LoopCountMax != 0))
		{
			LoopSource = Level;
			break;
		}
	}
	if (LoopSource->bPresent)
	{
		LoopDelayMin = LoopSource->LoopDelayMin;
		LoopDelayMax = LoopSource->LoopDelayMax;
	}
	const int32 LoopCount = !LoopSource->bPresent ? 0 : (LoopSource->LoopCountMax > LoopSource->LoopCountMin ? Random.RandRange(LoopSource->LoopCountMin, LoopSource->LoopCountMax) : LoopSource->LoopCountMin);
	RunsAllowed = LoopCount < 0 ? -1 : LoopCount + 1;

	ParticleCap = FMath::Clamp(FMath::FloorToInt(Params.MaxParticles), 1, Settings.MaxParticlesPerEmitter);

	// Flocks live in a box around the emitter's base translation: FlockParams[4] wide and [5] high (half-extents).
	CageCenter = FVector(Emitter->GetCurve(ESWGEmitterCurve::TranslateX).EvaluateValue(0.f), Emitter->GetCurve(ESWGEmitterCurve::TranslateY).EvaluateValue(0.f),
		Emitter->GetCurve(ESWGEmitterCurve::TranslateZ).EvaluateValue(0.f));
	CageHalfExtent = FVector(Params.FlockParams[4], Params.FlockParams[5], Params.FlockParams[4]).ComponentMax(FVector(0.01));

	bAligned = Emitter->IsQuad() && Params.Orientation != 0;

	Phase = EPhase::Delay;
	RunWindowSeconds = ComputeRunWindow();
}

float FSWGEmitterSimulation::SampleRange(float MinValue, float MaxValue)
{
	return MaxValue > MinValue ? Random.FRandRange(MinValue, MaxValue) : MinValue;
}

FVector FSWGEmitterSimulation::RandomUnitVector()
{
	return Random.VRand();
}

float FSWGEmitterSimulation::SampleCurve(ESWGEmitterCurve Curve, float CurveTime)
{
	return Emitter->GetCurve(Curve).Sample(CurveTime, Random.FRand());
}

FVector FSWGEmitterSimulation::SampleShapeVector()
{
	// Mirrors ParticleEmitterShape*::getPosition (0x7c5df0 and siblings), including its distributions.
	const auto RandomSign = [this]() { return Random.RandRange(0, 1) == 0 ? -1.0 : 1.0; };
	const auto RandomSigned = [this]() { return (double)Random.FRandRange(-1.f, 1.f); };
	switch (Emitter->Params.Shape)
	{
	case 0:
	{
		const float Angle = Random.FRandRange(-(float)UE_PI, (float)UE_PI);
		return FVector(FMath::Cos(Angle), 0.0, FMath::Sin(Angle));
	}
	case 1:
	{
		const float Polar = Random.FRandRange(-(float)UE_PI, (float)UE_PI);
		const float Azimuth = Random.FRandRange(-(float)UE_PI, (float)UE_PI);
		return FVector(FMath::Cos(Polar), FMath::Cos(Azimuth) * FMath::Sin(Polar), FMath::Sin(Azimuth) * FMath::Sin(Polar));
	}
	case 2:
		return Random.RandRange(0, 1) == 0 ? FVector(RandomSign(), 0.0, RandomSigned()) : FVector(RandomSigned(), 0.0, RandomSign());
	case 3:
	{
		FVector Point(RandomSigned(), RandomSigned(), RandomSigned());
		Point[Random.RandRange(0, 2)] = RandomSign();
		return Point;
	}
	case 4:
		return FVector(0.0, 0.0, RandomSign());
	case 5:
	{
		const double Sign = RandomSign();
		return Random.RandRange(0, 1) == 0 ? FVector(0.0, 0.0, Sign) : FVector(Sign, 0.0, 0.0);
	}
	default:
		return FVector::ZeroVector;
	}
}

float FSWGEmitterSimulation::ComputeRunWindow()
{
	const FSWGEmitterParams& Params = Emitter->Params;
	return FMath::Max(SampleRange(Params.RunTimeMin, Params.RunTimeMax), Settings.MinRunWindowSeconds);
}

void FSWGEmitterSimulation::Stop(bool bKillParticles)
{
	bStopRequested = true;
	Phase = EPhase::Finished;
	if (bKillParticles)
	{
		Particles.Reset();
	}
}

void FSWGEmitterSimulation::BeginRun(const FSWGEffectFrame& Frame, float TimeIntoStep, float StepLength)
{
	++RunsStarted;
	RunTime = 0.f;
	RunWindowSeconds = ComputeRunWindow();
	SpawnAccumulator = 0.f;
	DistanceAccumulator = 0.f;
	Phase = EPhase::Running;

	const FSWGEmitterParams& Params = Emitter->Params;
	bStartParticlePending = Params.bEmitOnStart && !Params.bBurst;
	if (Params.bTerrainCollide)
	{
		HorizontalBounce = SampleRange(Params.ParamFloats[0], Params.ParamFloats[1]);
		VerticalBounce = SampleRange(Params.ParamFloats[2], Params.ParamFloats[3]);
	}
	if (Params.bBurst)
	{
		FireBurst(TimeIntoStep, StepLength, Frame);
	}
}

bool FSWGEmitterSimulation::EndRun()
{
	if (bStopRequested || (RunsAllowed >= 0 && RunsStarted >= RunsAllowed))
	{
		Phase = EPhase::Finished;
		return false;
	}
	return true;
}

void FSWGEmitterSimulation::EmitDuring(float StepSeconds, float StepOffset, float StepLength, const FSWGEffectFrame& Frame)
{
	if (StepSeconds <= 0.f)
	{
		return;
	}

	const FSWGEmitterParams& Params = Emitter->Params;
	const float CurveTime = FMath::Clamp((RunTime + StepSeconds * 0.5f) / RunWindowSeconds, 0.f, 1.f);

	int32 SpawnCount = 0;
	if (Params.bGenerateByDistance)
	{
		// Rate is a spacing in metres here (inferred), so a stationary effect emits nothing.
		const float Spacing = FMath::Max(SampleCurve(ESWGEmitterCurve::Rate, CurveTime), Settings.MinDistanceSpacing);
		if (bHasPreviousFrame)
		{
			DistanceAccumulator += (float)FVector::Dist(PreviousFrame.Location, Frame.Location);
		}
		SpawnCount = FMath::FloorToInt(DistanceAccumulator / Spacing);
		DistanceAccumulator -= SpawnCount * Spacing;
	}
	else if (!Params.bBurst)
	{
		SpawnAccumulator += FMath::Max(SampleCurve(ESWGEmitterCurve::Rate, CurveTime), 0.f) * StepSeconds;
		SpawnCount = FMath::FloorToInt(SpawnAccumulator);
		SpawnAccumulator -= SpawnCount;
	}

	if (bStartParticlePending && !Params.bBurst)
	{
		bStartParticlePending = false;
		SpawnCount = FMath::Max(SpawnCount, 1);
	}

	const int32 Room = FMath::Max(0, ParticleCap - Particles.Num());
	if (SpawnCount > Room)
	{
		SpawnCount = Room;
		SpawnAccumulator = 0.f;
	}
	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		SpawnParticle(CurveTime, StepOffset + StepSeconds * (Index + 0.5f) / SpawnCount, StepLength, Frame);
	}
}

void FSWGEmitterSimulation::SpawnParticle(float CurveTime, float TimeIntoStep, float StepLength, const FSWGEffectFrame& Frame)
{
	const float StepAlpha = StepLength > UE_SMALL_NUMBER ? FMath::Clamp(TimeIntoStep / StepLength, 0.f, 1.f) : 1.f;
	const FSWGEffectFrame SpawnFrame = bHasPreviousFrame ? LerpFrame(PreviousFrame, Frame, StepAlpha) : Frame;

	FSWGSimParticle Particle;
	Particle.Id = NextParticleId++;
	Particle.Lifetime = FMath::Max(SampleCurve(ESWGEmitterCurve::Lifetime, CurveTime), 0.01f);
	Particle.BaseSpeed = SampleCurve(ESWGEmitterCurve::Speed, CurveTime);
	Particle.Gravity = SampleCurve(ESWGEmitterCurve::Gravity, CurveTime);
	Particle.RandomRotation = Random.FRand();
	Particle.RandomLength = Random.FRand();
	Particle.RandomWidth = Random.FRand();
	Particle.RandomAlpha = Random.FRand();
	Particle.RandomColor = Random.FRand();
	Particle.InitialRotation = Emitter->Params.bRandomInitialRotation ? Random.FRand() : 1.f;
	if (Emitter->Particle.bRandomSpinDirection && Random.RandRange(0, 1) == 0)
	{
		Particle.InitialRotation = -Particle.InitialRotation;
	}

	FVector LocalPosition;
	FVector LocalDirection;
	if (IsFlocking())
	{
		LocalPosition = CageCenter + FVector(Random.FRandRange(-1.f, 1.f), Random.FRandRange(-1.f, 1.f), Random.FRandRange(-1.f, 1.f)) * CageHalfExtent;
		LocalDirection = RandomUnitVector();
	}
	else
	{
		const FVector Translate(SampleCurve(ESWGEmitterCurve::TranslateX, CurveTime), SampleCurve(ESWGEmitterCurve::TranslateY, CurveTime), SampleCurve(ESWGEmitterCurve::TranslateZ, CurveTime));
		const FQuat EmitterRotation = MakeTurnsRotation(SampleCurve(ESWGEmitterCurve::RotateX, CurveTime), SampleCurve(ESWGEmitterCurve::RotateY, CurveTime),
			SampleCurve(ESWGEmitterCurve::RotateZ, CurveTime));
		const float Distance = SampleCurve(ESWGEmitterCurve::Distance, CurveTime);
		const float ShapeSize = SampleCurve(ESWGEmitterCurve::ShapeSize, CurveTime);
		const FVector ShapeVector = SampleShapeVector();
		FVector Offset;
		FVector Direction;
		if (Emitter->Params.EmissionStyle == 0)
		{
			// Leave along the shape vector; Distance widens the shape (0x7a3990, style 0).
			Offset = ShapeVector * (double)(ShapeSize + Distance);
			Direction = ShapeVector;
		}
		else
		{
			// Every particle leaves at exactly Spread degrees off +Y, at a random azimuth; Distance pushes along it.
			const float SpreadRadians = FMath::DegreesToRadians(SampleCurve(ESWGEmitterCurve::Spread, CurveTime));
			const float Azimuth = Random.FRandRange(-(float)UE_PI, (float)UE_PI);
			Direction = FVector(-FMath::Sin(SpreadRadians) * FMath::Cos(Azimuth), FMath::Cos(SpreadRadians), FMath::Sin(SpreadRadians) * FMath::Sin(Azimuth));
			Offset = ShapeVector * (double)ShapeSize + Direction * (double)Distance;
		}
		LocalPosition = Translate + EmitterRotation.RotateVector(Offset);
		LocalDirection = EmitterRotation.RotateVector(Direction);
	}

	if (bWorldSpace)
	{
		Particle.Position = SpawnFrame.Location + SpawnFrame.Rotation.RotateVector(LocalPosition * SpawnFrame.Scale);
		Particle.Direction = SpawnFrame.Rotation.RotateVector(LocalDirection);
	}
	else
	{
		Particle.Position = LocalPosition;
		Particle.Direction = LocalDirection;
	}

	if (Emitter->Params.bSnapToTerrain && Settings.TerrainHeight && FMath::Abs(SpawnFrame.Scale) > UE_SMALL_NUMBER)
	{
		FVector Position = ToWorldPosition(Particle, SpawnFrame);
		float Height;
		if (Settings.TerrainHeight(Position, Height) && FMath::IsFinite(Height))
		{
			Position.Y = Height + Emitter->Params.TerrainHeightOffset * SpawnFrame.Scale;
			Particle.Position = bWorldSpace ? Position : SpawnFrame.Rotation.UnrotateVector(Position - SpawnFrame.Location) / SpawnFrame.Scale;
			if (Emitter->Params.bTerrainZeroVelocity)
			{
				Particle.BaseSpeed = 0.f;
				Particle.ExtraVelocity = FVector::ZeroVector;
			}
		}
	}

	// PATT: ValueA..ValueB is read as the life-fraction window the child is requested in (0/0 = at birth, 1/1 = at death).
	if (Emitter->Particle.bHasChild && !Emitter->Particle.Child.Path.IsEmpty())
	{
		const FSWGParticleChild& Child = Emitter->Particle.Child;
		Particle.ChildTrigger = FMath::Clamp(SampleRange(FMath::Min(Child.ValueA, Child.ValueB), FMath::Max(Child.ValueA, Child.ValueB)), 0.f, 1.f);
	}

	// A particle spawned partway through a step has already lived the rest of it.
	const float ExtraTime = FMath::Max(0.f, StepLength - TimeIntoStep);
	AdvanceParticle(Particle, ExtraTime, Frame);
	Particles.Add(Particle);
}

FVector FSWGEmitterSimulation::ComputeVelocity(const FSWGSimParticle& Particle, const FSWGEffectFrame& Frame) const
{
	const float LifeFraction = FMath::Clamp(Particle.Age / Particle.Lifetime, 0.f, 1.f);
	// The PTCL speed curve multiplies the emitter's spawn speed (0x7a4840); flocks are held at twice the emitter speed.
	const float SpeedMultiplier = Emitter->Particle.Speed.IsEmpty() ? 1.f : Emitter->Particle.Speed.EvaluateValue(LifeFraction);
	const float Speed = Particle.BaseSpeed * SpeedMultiplier * (IsFlocking() ? 2.f : 1.f);
	const double UnitScale = bWorldSpace ? Frame.Scale : 1.0;
	return Particle.Direction * (double)Speed * UnitScale + Particle.ExtraVelocity;
}

void FSWGEmitterSimulation::AdvanceParticle(FSWGSimParticle& Particle, float DeltaSeconds, const FSWGEffectFrame& Frame)
{
	if (DeltaSeconds <= 0.f)
	{
		return;
	}

	const double UnitScale = bWorldSpace ? Frame.Scale : 1.0;
	const FVector GravityDirection = bWorldSpace ? -SimUp : Frame.Rotation.UnrotateVector(-SimUp);
	// Wind only moves world-space particles, by what their wind resistance lets through (0x7a4840).
	const FVector WindDrift = bWorldSpace ? Frame.Wind * (double)(1.f - Emitter->Params.GetWindResistance()) : FVector::ZeroVector;

	Particle.ExtraVelocity += GravityDirection * (double)(Particle.Gravity * GravityMetresPerSecondSquared * DeltaSeconds) * UnitScale;
	Particle.Position += (ComputeVelocity(Particle, Frame) + WindDrift) * (double)DeltaSeconds;
	Particle.Age += DeltaSeconds;
	if (Emitter->Params.bTerrainCollide && Settings.TerrainHeight && FMath::Abs(Frame.Scale) > UE_SMALL_NUMBER)
	{
		FVector Position = ToWorldPosition(Particle, Frame);
		float Height;
		if (Settings.TerrainHeight(Position, Height) && FMath::IsFinite(Height) && Position.Y <= Height + Emitter->Params.GroundOffset * Frame.Scale)
		{
			Particle.bTerrainHit = true;
			Position.Y = Height + Emitter->Params.GroundOffset * Frame.Scale;
			Particle.Position = bWorldSpace ? Position : Frame.Rotation.UnrotateVector(Position - Frame.Location) / Frame.Scale;
			if (Emitter->Params.bKillOnTerrainHit)
			{
				Particle.Age = Particle.Lifetime;
			}
			else
			{
				const FVector Velocity = ComputeVelocity(Particle, Frame);
				FVector WorldVelocity = bWorldSpace ? Velocity : Frame.Rotation.RotateVector(Velocity * Frame.Scale);
				if (WorldVelocity.Y < 0.0)
				{
					WorldVelocity.X *= HorizontalBounce;
					WorldVelocity.Z *= HorizontalBounce;
					WorldVelocity.Y *= -VerticalBounce;
					const FVector Bounced = bWorldSpace ? WorldVelocity : Frame.Rotation.UnrotateVector(WorldVelocity) / Frame.Scale;
					Particle.ExtraVelocity += Bounced - Velocity;
				}
			}
		}
	}

	if (IsFlocking())
	{
		// Hard cage: clamp and bounce so a flock never leaves its box.
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Low = CageCenter[Axis] - CageHalfExtent[Axis];
			const double High = CageCenter[Axis] + CageHalfExtent[Axis];
			if (Particle.Position[Axis] < Low || Particle.Position[Axis] > High)
			{
				Particle.Position[Axis] = FMath::Clamp(Particle.Position[Axis], Low, High);
				Particle.Direction[Axis] = -Particle.Direction[Axis];
			}
		}
	}
}

void FSWGEmitterSimulation::StepFlock(float DeltaSeconds)
{
	// FlockParams (0x7a4840): [0] separation radius m, [1] alignment gain, [2] cohesion gain, [3] separation gain.
	// Retail steers the velocity directly; this keeps a direction-only model and a fixed turn rate.
	const FSWGEmitterParams& Params = Emitter->Params;
	const float Radius = FMath::Max(Params.FlockParams[0], 0.1f);
	const float RadiusSquared = Radius * Radius;
	const float TurnRate = FMath::Max(Settings.FlockTurnRateScale * 4.f, 0.2f);
	const int32 Count = Particles.Num();

	TArray<FVector> NewDirections;
	NewDirections.SetNumUninitialized(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FSWGSimParticle& Particle = Particles[Index];
		FVector Separation = FVector::ZeroVector;
		FVector AverageDirection = FVector::ZeroVector;
		FVector Centroid = FVector::ZeroVector;
		int32 Neighbours = 0;
		const int32 Checks = FMath::Min(Count - 1, Settings.FlockMaxNeighbours);
		for (int32 Offset = 1; Offset <= Checks; ++Offset)
		{
			const FSWGSimParticle& Other = Particles[(Index + Offset) % Count];
			const FVector Delta = Other.Position - Particle.Position;
			const double DistanceSquared = Delta.SizeSquared();
			if (DistanceSquared < RadiusSquared)
			{
				Separation -= Delta / FMath::Max(DistanceSquared, 0.01);
				AverageDirection += Other.Direction;
				Centroid += Other.Position;
				++Neighbours;
			}
		}

		FVector Steer = RandomUnitVector() * (double)Settings.FlockWanderRadiansPerSecond / TurnRate;
		if (Neighbours > 0)
		{
			Steer += Separation * (double)Params.FlockParams[3];
			Steer += AverageDirection.GetSafeNormal() * (double)Params.FlockParams[1];
			Steer += (Centroid / (double)Neighbours - Particle.Position).GetSafeNormal() * (double)Params.FlockParams[2];
		}

		// Push back toward the middle once past the soft edge of the cage.
		const FVector Relative = Particle.Position - CageCenter;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Ratio = FMath::Abs(Relative[Axis]) / CageHalfExtent[Axis];
			if (Ratio > Settings.FlockCageSoftFraction)
			{
				Steer[Axis] -= FMath::Sign(Relative[Axis]) * (Ratio - Settings.FlockCageSoftFraction) / (1.0 - Settings.FlockCageSoftFraction) * 4.0;
			}
		}

		const FVector NewDirection = (Particle.Direction + Steer * (double)(TurnRate * DeltaSeconds)).GetSafeNormal();
		NewDirections[Index] = NewDirection.IsNearlyZero() ? Particle.Direction : NewDirection;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Particles[Index].Direction = NewDirections[Index];
	}
}

void FSWGEmitterSimulation::CheckChild(FSWGSimParticle& Particle, const FSWGEffectFrame& Frame, TArray<FSWGChildSpawnRequest>& OutChildren) const
{
	const bool bTerrainChild = Emitter->Particle.Child.Mode == 3;
	if (Particle.ChildTrigger < 0.f || (bTerrainChild ? !Particle.bTerrainHit : Particle.Age / Particle.Lifetime < Particle.ChildTrigger))
	{
		return;
	}
	Particle.ChildTrigger = -1.f;

	FSWGChildSpawnRequest Request;
	Request.EffectPath = Emitter->Particle.Child.Path;
	Request.EmitterIndex = EmitterIndex;
	Request.ParticleId = Particle.Id;
	Request.WorldPosition = ToWorldPosition(Particle, Frame);
	Request.WorldRotation = Frame.Rotation;
	Request.bFollowParticle = Emitter->Particle.Child.Mode == 2;
	OutChildren.Add(MoveTemp(Request));
}

void FSWGEmitterSimulation::Update(float DeltaSeconds, const FSWGEffectFrame& Frame, TArray<FSWGChildSpawnRequest>& OutChildren)
{
	if (DeltaSeconds < 0.f)
	{
		DeltaSeconds = 0.f;
	}

	if (IsFlocking() && DeltaSeconds > 0.f)
	{
		StepFlock(DeltaSeconds);
	}

	for (int32 Index = Particles.Num() - 1; Index >= 0; --Index)
	{
		FSWGSimParticle& Particle = Particles[Index];
		AdvanceParticle(Particle, DeltaSeconds, Frame);
		CheckChild(Particle, Frame, OutChildren);
		if (Particle.Age >= Particle.Lifetime)
		{
			Particles.RemoveAtSwap(Index);
		}
	}

	if (Phase == EPhase::Delay && DelayRemaining <= 0.f)
	{
		BeginRun(Frame, 0.f, DeltaSeconds);
	}

	float Remaining = DeltaSeconds;
	float Consumed = 0.f;
	for (int32 Guard = 0; Phase != EPhase::Finished && Guard < 10000; ++Guard)
	{
		if (Phase == EPhase::Drain)
		{
			if (!Particles.IsEmpty())
			{
				break;
			}
			Phase = EPhase::Delay;
			DelayRemaining = SampleRange(LoopDelayMin, LoopDelayMax);
		}
		else if (Phase == EPhase::Delay)
		{
			if (Remaining <= 0.f && DelayRemaining > 0.f)
			{
				break;
			}
			const float Wait = FMath::Min(DelayRemaining, Remaining);
			DelayRemaining -= Wait;
			Remaining -= Wait;
			Consumed += Wait;
			if (DelayRemaining <= 0.f)
			{
				BeginRun(Frame, Consumed, DeltaSeconds);
			}
		}
		else
		{
			if (Remaining <= 0.f)
			{
				break;
			}
			const float Step = FMath::Min(Remaining, RunWindowSeconds - RunTime);
			EmitDuring(Step, Consumed, DeltaSeconds, Frame);
			RunTime += Step;
			Remaining -= Step;
			Consumed += Step;
			if (RunTime >= RunWindowSeconds - 1e-5f && EndRun())
			{
				if (Emitter->Params.bIgnoreLiveParticles)
				{
					Phase = EPhase::Delay;
					DelayRemaining = SampleRange(LoopDelayMin, LoopDelayMax);
				}
				else
				{
					Phase = EPhase::Drain;
				}
			}
		}
	}

	// Particles born this step may already be due (birth-time children).
	for (int32 Index = Particles.Num() - 1; Index >= 0; --Index)
	{
		FSWGSimParticle& Particle = Particles[Index];
		CheckChild(Particle, Frame, OutChildren);
		if (Particle.Age >= Particle.Lifetime) Particles.RemoveAtSwap(Index);
	}

	PreviousFrame = Frame;
	bHasPreviousFrame = true;
}

FVector FSWGEmitterSimulation::ToWorldPosition(const FSWGSimParticle& Particle, const FSWGEffectFrame& Frame) const
{
	return bWorldSpace ? Particle.Position : Frame.Location + Frame.Rotation.RotateVector(Particle.Position * Frame.Scale);
}

bool FSWGEmitterSimulation::TryGetParticleWorldPosition(uint32 ParticleId, const FSWGEffectFrame& Frame, FVector& OutPosition) const
{
	for (const FSWGSimParticle& Particle : Particles)
	{
		if (Particle.Id == ParticleId)
		{
			OutPosition = ToWorldPosition(Particle, Frame);
			return true;
		}
	}
	return false;
}

int32 FSWGEmitterSimulation::ComputeFlipbookFrame(const FSWGParticleTexture& Texture, float AgeSeconds, float LifeFraction)
{
	const int32 FrameSpan = FMath::Max(Texture.FrameEnd - Texture.FrameStart + 1, 1);
	int32 Index = 0;
	if (Texture.FramesPerSecond == -1.f)
	{
		Index = FMath::Clamp(FMath::FloorToInt(LifeFraction * FrameSpan), 0, FrameSpan - 1);
	}
	else if (Texture.FramesPerSecond > 0.f && AgeSeconds > 0.f)
	{
		Index = FMath::FloorToInt(AgeSeconds * Texture.FramesPerSecond) % FrameSpan;
	}
	return Texture.FrameStart + Index;
}

void FSWGEmitterSimulation::GatherRenderData(const FSWGEffectFrame& Frame, TArray<FSWGParticleRenderData>& OutData) const
{
	const FSWGParticleDescription& Description = Emitter->Particle;
	const bool bMesh = Emitter->Kind == ESWGParticleKind::Mesh;
	const float Scale = (float)Frame.Scale;

	for (const FSWGSimParticle& Particle : Particles)
	{
		const float LifeFraction = FMath::Clamp(Particle.Age / Particle.Lifetime, 0.f, 1.f);

		FSWGParticleRenderData Data;
		Data.Id = Particle.Id;
		Data.LifeFraction = LifeFraction;
		Data.WorldPosition = ToWorldPosition(Particle, Frame);
		FVector Velocity = ComputeVelocity(Particle, Frame);
		if (!bWorldSpace)
		{
			Velocity = Frame.Rotation.RotateVector(Velocity * Frame.Scale);
		}
		Data.WorldVelocity = Velocity;

		Data.Color = Description.Color.Evaluate(Description.Color.Mode1 == 1 ? Particle.RandomColor : LifeFraction);
		Data.Color.A = FMath::Clamp(Description.Alpha.IsEmpty() ? 1.f : Description.Alpha.Sample(LifeFraction, Particle.RandomAlpha), 0.f, 1.f);

		if (bMesh)
		{
			const TArray<FSWGWaveform>& Curves = Emitter->ShapeCurves;
			if (Curves.Num() >= (int32)ESWGMeshCurve::Count)
			{
				Data.Width = Data.Length = Curves[(int32)ESWGMeshCurve::Scale].Sample(LifeFraction, Particle.RandomWidth) * Scale;
				Data.MeshRotation = MakeTurnsRotation(Curves[(int32)ESWGMeshCurve::RotateX].Sample(LifeFraction, Particle.RandomRotation),
					Curves[(int32)ESWGMeshCurve::RotateY].Sample(LifeFraction, Particle.RandomRotation), Curves[(int32)ESWGMeshCurve::RotateZ].Sample(LifeFraction, Particle.RandomRotation));
			}
			else
			{
				Data.Width = Data.Length = Scale;
			}
		}
		else
		{
			const TArray<FSWGWaveform>& Curves = Emitter->ShapeCurves;
			if (Curves.Num() >= (int32)ESWGQuadCurve::Count)
			{
				// (curve + initial) turns, negated when the initial rotation is negative (0x7a5ef0).
				const float Turns = Curves[(int32)ESWGQuadCurve::Rotation].Sample(LifeFraction, Particle.RandomRotation) + Particle.InitialRotation;
				Data.RotationRadians = Turns * Settings.QuadRotationRadiansPerUnit * (Particle.InitialRotation < 0.f ? -1.f : 1.f);
				Data.Length = FMath::Max(Curves[(int32)ESWGQuadCurve::Length].Sample(LifeFraction, Particle.RandomLength), 0.f) * Scale;
				Data.Width = Emitter->bWidthFromLength ? Data.Length : FMath::Max(Curves[(int32)ESWGQuadCurve::Width].Sample(LifeFraction, Particle.RandomWidth), 0.f) * Scale;
			}
			else
			{
				Data.Length = Data.Width = 0.25f * Scale;
			}
			Data.Frame = ComputeFlipbookFrame(Emitter->Texture, Particle.Age, LifeFraction);

			if (bAligned)
			{
				// Along the velocity, or the spawn direction when the particle is (nearly) still, as with bolts.
				const FVector VelocityAxis = Velocity.GetSafeNormal();
				Data.AlignAxis = VelocityAxis.IsNearlyZero() ? (bWorldSpace ? Particle.Direction : Frame.Rotation.RotateVector(Particle.Direction)).GetSafeNormal() : VelocityAxis;
			}
		}
		OutData.Add(Data);
	}
}

void FSWGEffectSimulation::Initialize(TSharedRef<const FSWGParticleEffect> InEffect, const FSWGEffectSimSettings& InSettings)
{
	Effect = InEffect;
	Emitters.Reset();
	PendingChildren.Reset();
	ElapsedSeconds = 0.f;
	bStopped = false;

	int32 EmitterIndex = 0;
	for (const FSWGEmitterGroup& Group : Effect->Groups)
	{
		for (const FSWGParticleEmitter& Emitter : Group.Emitters)
		{
			FSWGEmitterSimulation& Simulation = Emitters.AddDefaulted_GetRef();
			Simulation.Initialize(Emitter, Effect->Timing, Group.Timing, InSettings, HashCombineFast(InSettings.Seed, (uint32)EmitterIndex * 2654435761u + 1u), EmitterIndex);
			++EmitterIndex;
		}
	}
}

void FSWGEffectSimulation::Update(float DeltaSeconds, const FSWGEffectFrame& Frame)
{
	ElapsedSeconds += DeltaSeconds;
	for (FSWGEmitterSimulation& Emitter : Emitters)
	{
		Emitter.Update(DeltaSeconds, Frame, PendingChildren);
	}
}

void FSWGEffectSimulation::Stop(bool bImmediate)
{
	bStopped = true;
	for (FSWGEmitterSimulation& Emitter : Emitters)
	{
		Emitter.Stop(bImmediate);
	}
}

bool FSWGEffectSimulation::IsFinished() const
{
	for (const FSWGEmitterSimulation& Emitter : Emitters)
	{
		if (!Emitter.IsFinished())
		{
			return false;
		}
	}
	return true;
}

int32 FSWGEffectSimulation::GetLiveParticleCount() const
{
	int32 Total = 0;
	for (const FSWGEmitterSimulation& Emitter : Emitters)
	{
		Total += Emitter.GetParticleCount();
	}
	return Total;
}

TArray<FSWGChildSpawnRequest> FSWGEffectSimulation::ConsumeChildRequests()
{
	TArray<FSWGChildSpawnRequest> Result = MoveTemp(PendingChildren);
	PendingChildren.Reset();
	return Result;
}

void FSWGEmitterSimulation::FireBurst(float TimeIntoStep, float StepLength, const FSWGEffectFrame& Frame)
{
	// A batch that would not fit under the cap is skipped whole, and a live emitter only re-fires when asked to (0x7a6980).
	const FSWGEmitterParams& Params = Emitter->Params;
	const int32 Count = FMath::Max(0, FMath::RoundToInt(SampleRange((float)Params.CountMin, (float)Params.CountMax)));
	if ((!Particles.IsEmpty() && !Params.bIgnoreLiveParticles) || Particles.Num() + Count > ParticleCap)
	{
		return;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		SpawnParticle(0.f, TimeIntoStep, StepLength, Frame);
	}
}
