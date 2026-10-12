#include "Vehicle/SWGVehicleDynamicsComponent.h"

#include "Components/SWGMovementComponent.h"
#include "Common/SWGWorldScale.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Objects/Creature/SWGCreature.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "SWGAudioSubsystem.h"
#include "SWGEffectsSubsystem.h"
#include "Objects/SWGObject.h"
#include "Subsystems/SWGTerrainSubsystem.h"

namespace
{
	constexpr float EngineFadeInSeconds = 0.5f;
	constexpr float EngineFadeOutSeconds = 0.4f;
	constexpr float MinProbeSpread = 80.f;
	constexpr float MaxProbeSpread = 300.f;
}

USWGVehicleDynamicsComponent::USWGVehicleDynamicsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void USWGVehicleDynamicsComponent::Setup(const FString& InClientDataPath, const FSWGClientDataFile& InClientData)
{
	ClientDataPath = InClientDataPath;
	EngineSounds = FSWGVehicleEngineSounds::FromClientData(InClientData);
	ThrustLevels = InClientData.Thrusts.Num();
	Thrusts = InClientData.Thrusts;
	GroundEffects = InClientData.GroundEffects;
}

USWGAudioSubsystem* USWGVehicleDynamicsComponent::GetAudio() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<USWGAudioSubsystem>() : nullptr;
}

UPrimitiveComponent* USWGVehicleDynamicsComponent::FindBody()
{
	if (!Body.IsValid())
	{
		const TArray<UActorComponent*> Tagged = GetOwner()->GetComponentsByTag(UPrimitiveComponent::StaticClass(), USWGMeshGeneratorSubsystem::VehicleBodyTag);
		Body = Tagged.IsEmpty() ? nullptr : Cast<UPrimitiveComponent>(Tagged[0]);
	}
	return Body.Get();
}

void USWGVehicleDynamicsComponent::SampleTerrain(float& OutPitch, float& OutRoll, float& OutHeight, bool& bOutHasGround) const
{
	const AActor* Owner = GetOwner();
	const UPrimitiveComponent* BodyComponent = Body.Get();
	const float Spread = BodyComponent
		? FMath::Clamp(static_cast<float>(BodyComponent->Bounds.BoxExtent.Size2D()) * 0.5f, MinProbeSpread, MaxProbeSpread)
		: MinProbeSpread;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SWGVehicleHover), false, Owner);
	TArray<AActor*> Attached;
	Owner->GetAttachedActors(Attached);
	QueryParams.AddIgnoredActors(Attached);

	const FVector Origin = Owner->GetActorLocation();
	const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = Owner->GetActorRightVector().GetSafeNormal2D();

	auto SampleHeight = [&](const FVector& Offset, float& OutZ) -> bool
	{
		FHitResult Hit;
		const FVector Start = Origin + Offset + FVector(0.0, 0.0, 200.0);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0, 0.0, 1000.0), ECC_Visibility, QueryParams))
		{
			OutZ = static_cast<float>(Hit.ImpactPoint.Z);
			return true;
		}
		return false;
	};

	float Center = 0.f, Front = 0.f, Back = 0.f, Left = 0.f, RightZ = 0.f;
	bOutHasGround = SampleHeight(FVector::ZeroVector, Center);
	OutPitch = 0.f;
	OutRoll = 0.f;
	OutHeight = Center;
	if (bOutHasGround && SampleHeight(Forward * Spread, Front) && SampleHeight(-Forward * Spread, Back))
	{
		OutPitch = FMath::Atan2(Front - Back, 2.f * Spread);
	}
	if (bOutHasGround && SampleHeight(Right * Spread, RightZ) && SampleHeight(-Right * Spread, Left))
	{
		// Right side higher than the left rolls the vehicle left (negative).
		OutRoll = -FMath::Atan2(RightZ - Left, 2.f * Spread);
	}
}

void USWGVehicleDynamicsComponent::ApplyTilt(const FRotator& Tilt, float HeightOffset)
{
	UPrimitiveComponent* BodyComponent = FindBody();
	if (!BodyComponent)
	{
		return;
	}
	if (!bBodyBaseCaptured)
	{
		BodyBase = BodyComponent->GetRelativeTransform();
		bBodyBaseCaptured = true;
	}

	const FTransform TiltTransform(Tilt, FVector(0.0, 0.0, HeightOffset));
	BodyComponent->SetRelativeTransform(BodyBase * TiltTransform);

	// Riders sit in the body's frame, so they follow the tilt about the same origin.
	const ASWGCreature* Mount = Cast<ASWGCreature>(GetOwner());
	if (!Mount || !Mount->RiderSeatTransform.IsSet())
	{
		return;
	}
	TArray<AActor*> Attached;
	GetOwner()->GetAttachedActors(Attached);
	for (AActor* Rider : Attached)
	{
		const ASWGCreature* RiderCreature = Cast<ASWGCreature>(Rider);
		if (RiderCreature && RiderCreature->RiddenMount.Get() == Mount)
		{
			Rider->SetActorRelativeTransform(*Mount->RiderSeatTransform * TiltTransform);
		}
	}
}

void USWGVehicleDynamicsComponent::StartLoop(FSWGSoundHandle& Handle, const FString& SoundPath, float FadeInSeconds)
{
	Handle.Stop(EngineFadeOutSeconds);
	Handle = FSWGSoundHandle();
	USWGAudioSubsystem* Audio = GetAudio();
	if (!Audio || SoundPath.IsEmpty() || !GetOwner()->GetRootComponent())
	{
		return;
	}
	FSWGSoundPlayOptions Options;
	Options.FadeInSeconds = FadeInSeconds;
	Handle = Audio->PlaySoundAttached(SoundPath, GetOwner()->GetRootComponent(), NAME_None, Options);
}

void USWGVehicleDynamicsComponent::UpdateEngine(float SpeedFraction)
{
	USWGAudioSubsystem* Audio = GetAudio();
	if (!Audio || !EngineSounds.IsValid())
	{
		return;
	}

	if (!bEngineStarted)
	{
		bEngineStarted = true;
		EngineState = SpeedFraction > FSWGVehicleEngine::RunAbove ? ESWGEngineState::Running : ESWGEngineState::Idle;
		StartLoop(EngineState == ESWGEngineState::Running ? RunHandle : IdleHandle,
			EngineState == ESWGEngineState::Running ? EngineSounds.Run : EngineSounds.Idle, EngineFadeInSeconds);
		return;
	}

	const FSWGEngineStep Step = FSWGVehicleEngine::Update(EngineState, SpeedFraction);
	if (Step.State == EngineState)
	{
		// A loop that ended (distance cull, sample evicted) restarts so the engine never goes silent.
		FSWGSoundHandle& Active = EngineState == ESWGEngineState::Running ? RunHandle : IdleHandle;
		if (!Active.IsPlaying())
		{
			StartLoop(Active, EngineState == ESWGEngineState::Running ? EngineSounds.Run : EngineSounds.Idle, EngineFadeInSeconds);
		}
		return;
	}

	EngineState = Step.State;
	FSWGSoundPlayOptions OneShot;
	USceneComponent* Root = GetOwner()->GetRootComponent();
	if (Step.bPlayAccel && !EngineSounds.Accel.IsEmpty() && Root)
	{
		Audio->PlaySoundAttached(EngineSounds.Accel, Root, NAME_None, OneShot);
	}
	if (Step.bPlayDecel && !EngineSounds.Decel.IsEmpty() && Root)
	{
		Audio->PlaySoundAttached(EngineSounds.Decel, Root, NAME_None, OneShot);
	}
	if (EngineState == ESWGEngineState::Running)
	{
		IdleHandle.Stop(EngineFadeOutSeconds);
		StartLoop(RunHandle, EngineSounds.Run, EngineFadeInSeconds);
	}
	else
	{
		RunHandle.Stop(EngineFadeOutSeconds);
		StartLoop(IdleHandle, EngineSounds.Idle, EngineFadeInSeconds);
	}
}

void USWGVehicleDynamicsComponent::StopEngine(float FadeSeconds)
{
	IdleHandle.Stop(FadeSeconds);
	RunHandle.Stop(FadeSeconds);
	bEngineStarted = false;
}

void USWGVehicleDynamicsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopEngine(0.f);
	StopEffects();
	Super::EndPlay(EndPlayReason);
}

void USWGVehicleDynamicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner || DeltaTime <= 0.f)
	{
		return;
	}

	// Speeds come from the mount's own movement setup (template speed/turnRate), so the fractions match what the server allows.
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Params.MaxSpeed = FMath::Max(Movement->MaxWalkSpeed, 100.f);
		}
		if (const USWGMovementComponent* SwgMovement = Cast<USWGMovementComponent>(Character->GetCharacterMovement()))
		{
			const float RunTurnRadians = FMath::DegreesToRadians(SwgMovement->GetTemplateRunTurnRate());
			Params.TurnRateAtMax = RunTurnRadians;
			Params.TurnRateAtRest = RunTurnRadians * 2.f;
		}
	}

	FindBody();

	FSWGHoverInput Input;
	Input.DeltaSeconds = DeltaTime;
	Input.Speed = static_cast<float>(Owner->GetVelocity().Size2D());
	Input.Forward = FVector2D(Owner->GetActorForwardVector().X, Owner->GetActorForwardVector().Y).GetSafeNormal();
	SampleTerrain(Input.GroundPitch, Input.GroundRoll, Input.GroundHeight, Input.bHasGround);

	LastOutput = FSWGVehicleHoverDynamics::Step(Params, State, Input);
	ApplyTilt(FRotator(FMath::RadiansToDegrees(LastOutput.Pitch), 0.f, FMath::RadiansToDegrees(LastOutput.Roll)), LastOutput.HeightOffset);
	UpdateEngine(LastOutput.SpeedFraction);
	UpdateEffects(LastOutput.SpeedFraction);
}

USceneComponent* USWGVehicleDynamicsComponent::MakeEffectAnchor(const FString& Hardpoint)
{
	AActor* Owner = GetOwner();
	USceneComponent* Parent = FindBody();
	if (!Parent)
	{
		Parent = Owner->GetRootComponent();
	}
	if (!Parent)
	{
		return nullptr;
	}
	USceneComponent* Anchor = NewObject<USceneComponent>(Owner, NAME_None, RF_Transient);
	Anchor->SetupAttachment(Parent);
	if (const ASWGObject* SwgObject = Cast<ASWGObject>(Owner))
	{
		if (const FTransform* Found = SwgObject->MeshHardpoints.Find(FName(*Hardpoint)))
		{
			Anchor->SetRelativeTransform(*Found);
		}
	}
	Anchor->RegisterComponent();
	EffectAnchors.Add(Anchor);
	return Anchor;
}

void USWGVehicleDynamicsComponent::StopEffects()
{
	for (const FSWGEffectHandle& Handle : EffectHandles)
	{
		Handle.Stop(false);
	}
	EffectHandles.Reset();
	for (USceneComponent* Anchor : EffectAnchors)
	{
		if (IsValid(Anchor))
		{
			Anchor->DestroyComponent();
		}
	}
	EffectAnchors.Reset();
	ActiveThrustIndex = -1;
	ActiveGroundKind = -1;
}

void USWGVehicleDynamicsComponent::UpdateEffects(float SpeedFraction)
{
	USWGEffectsSubsystem* Effects = GetWorld() ? GetWorld()->GetSubsystem<USWGEffectsSubsystem>() : nullptr;
	if (!Effects || (Thrusts.IsEmpty() && GroundEffects.IsEmpty()) || !FindBody())
	{
		return;
	}

	// Thruster level: the highest VTHR whose start fraction the speed has reached.
	int32 ThrustIndex = Thrusts.IsEmpty() ? -1 : 0;
	for (int32 Index = 0; Index < Thrusts.Num(); ++Index)
	{
		ThrustIndex = Thrusts[Index].Level <= SpeedFraction ? Index : ThrustIndex;
	}

	// Ground trail: none while (nearly) stopped, water when the vehicle is over a water surface.
	int32 GroundKind = -1;
	if (SpeedFraction > FSWGVehicleEngine::RunAbove && !GroundEffects.IsEmpty())
	{
		GroundKind = 0;
		const UGameInstance* GameInstance = GetWorld()->GetGameInstance();
		const USWGTerrainSubsystem* Terrain = GameInstance ? GameInstance->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
		if (Terrain && Terrain->IsTerrainLoaded())
		{
			const FVector Raw = SWGToRawSpace(GetOwner()->GetActorLocation());
			const FSWGWaterInfo Water = Terrain->GetWaterInfoAt(static_cast<float>(Raw.X), static_cast<float>(Raw.Y), static_cast<float>(Raw.Z), 1.f);
			GroundKind = Water.bHasWater && Water.WaterHeight > Water.GroundHeight ? 1 : 0;
		}
	}

	if (ThrustIndex == ActiveThrustIndex && GroundKind == ActiveGroundKind)
	{
		return;
	}

	StopEffects();
	ActiveThrustIndex = ThrustIndex;
	ActiveGroundKind = GroundKind;
	if (ThrustIndex >= 0)
	{
		for (const FSWGClientDataHardpointObject& Thruster : Thrusts[ThrustIndex].Thrusters)
		{
			if (USceneComponent* Anchor = MakeEffectAnchor(Thruster.Hardpoint))
			{
				EffectHandles.Add(Effects->PlayParticleAttached(Thruster.AppearancePath, Anchor));
			}
		}
	}
	if (GroundKind >= 0)
	{
		for (const FSWGClientDataGroundEffect& Ground : GroundEffects)
		{
			if (Ground.bWater == (GroundKind == 1))
			{
				if (USceneComponent* Anchor = MakeEffectAnchor(Ground.Hardpoint))
				{
					EffectHandles.Add(Effects->PlayParticleAttached(Ground.AppearancePath, Anchor));
				}
			}
		}
	}
}
