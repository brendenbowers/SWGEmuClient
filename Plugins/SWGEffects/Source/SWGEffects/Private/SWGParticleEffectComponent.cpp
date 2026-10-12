#include "SWGParticleEffectComponent.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "SWGAudioSubsystem.h"
#include "SWGEffects.h"
#include "SWGEffectsSubsystem.h"

namespace
{
	/** A quad is 100 cm across at scale 1; instances are scaled to their size so bounds and culling stay honest. */
	constexpr float MinInstanceScale = 0.05f;
}

void USWGParticleEffectComponent::BeginEffect(USWGEffectsSubsystem& InSubsystem, const FString& InPath, TSharedRef<const FSWGParticleEffect> Effect,
	const FSWGEffectPlayParams& Params, uint32 InSerial)
{
	Subsystem = &InSubsystem;
	EffectPath = InPath;
	PlayParams = Params;
	Serial = InSerial;
	bActive = true;
	bCulled = false;
	bHidden = false;
	SecondsCulled = 0.f;
	Children.Reset();
	PlayingSounds.Reset();
	PendingSounds.Reset();

	FSWGEffectSimSettings Settings = InSubsystem.MakeSimSettings();
	Settings.Seed = Params.Seed;
	Simulation.Initialize(Effect, Settings);

	const int32 EmitterCount = Simulation.GetEmitterCount();
	QuadInstances.Reset();
	QuadInstances.SetNum(EmitterCount);
	MeshEmitters.Reset();
	MeshEmitters.SetNum(EmitterCount);

	for (int32 EmitterIndex = 0; EmitterIndex < EmitterCount; ++EmitterIndex)
	{
		const FSWGEmitterSimulation& Emitter = Simulation.GetEmitter(EmitterIndex);
		const FSWGParticleEmitter& Data = Emitter.GetEmitterData();

		if (Data.Kind == ESWGParticleKind::Mesh)
		{
			if (Data.MeshPath.EndsWith(TEXT(".sat"), ESearchCase::IgnoreCase))
			{
				MeshEmitters[EmitterIndex].bSkeletal = true;
				MeshEmitters[EmitterIndex].SatPath = Data.MeshPath.Replace(TEXT("\\"), TEXT("/"));
			}
			else if (!Data.MeshPath.IsEmpty())
			{
				MeshEmitters[EmitterIndex].bRequested = true;
				TWeakObjectPtr<USWGParticleEffectComponent> WeakThis(this);
				const uint32 RequestSerial = Serial;
				InSubsystem.RequestParticleMesh(Data.MeshPath, [WeakThis, RequestSerial, EmitterIndex](UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
				{
					if (WeakThis.IsValid() && WeakThis->Serial == RequestSerial)
					{
						WeakThis->OnMeshReady(EmitterIndex, Mesh, Materials);
					}
				});
			}
		}
		else if (UInstancedStaticMeshComponent* Instances = InSubsystem.AcquireInstanceComponent())
		{
			Instances->SetMaterial(0, InSubsystem.GetParticleMaterial(Data.Texture.ShaderPath, Data.Texture.GridColumns));
			QuadInstances[EmitterIndex] = Instances;
		}

		if (Params.bPlaySounds && !Data.Params.SoundPath.IsEmpty())
		{
			FPendingSound& Sound = PendingSounds.AddDefaulted_GetRef();
			Sound.DelaySeconds = Emitter.GetStartDelay();
			Sound.SoundPath = Data.Params.SoundPath;
		}
	}
}

void USWGParticleEffectComponent::OnMeshReady(int32 EmitterIndex, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
{
	if (!MeshEmitters.IsValidIndex(EmitterIndex))
	{
		return;
	}
	FSWGMeshEmitterRender& Emitter = MeshEmitters[EmitterIndex];
	Emitter.Mesh = Mesh;
	Emitter.Materials.Reset();
	for (UMaterialInterface* Material : Materials)
	{
		Emitter.Materials.Add(Material);
	}
}

FSWGEffectFrame USWGParticleEffectComponent::MakeFrame() const
{
	FSWGEffectFrame Frame;
	Frame.Location = SWGFx::UnrealToSim(GetComponentLocation());
	Frame.Rotation = SWGFx::UnrealToSimRotation(GetComponentQuat());
	Frame.Scale = PlayParams.Scale * GetComponentScale().X;
	Frame.Wind = Subsystem ? Subsystem->GetWind() : FVector::ZeroVector;
	return Frame;
}

void USWGParticleEffectComponent::SetEmittersVisible(bool bMakeVisible)
{
	bHidden = !bMakeVisible;
	for (UInstancedStaticMeshComponent* Instances : QuadInstances)
	{
		if (Instances)
		{
			Instances->SetVisibility(bMakeVisible);
		}
	}
	for (FSWGMeshEmitterRender& Emitter : MeshEmitters)
	{
		for (UStaticMeshComponent* Particle : Emitter.Active)
		{
			if (Particle)
			{
				Particle->SetVisibility(bMakeVisible);
			}
		}
		for (const FSWGSkeletalParticleRender& Particle : Emitter.Skeletal)
		{
			if (Particle.Mesh)
			{
				Particle.Mesh->SetVisibility(bMakeVisible && Particle.bReady);
				Particle.Mesh->SetComponentTickEnabled(bMakeVisible);
			}
		}
	}
}

void USWGParticleEffectComponent::TickEffect(float DeltaSeconds, bool bInCulled)
{
	if (!bActive || !Subsystem)
	{
		return;
	}

	bCulled = bInCulled;
	if (bCulled)
	{
		SecondsCulled += DeltaSeconds;
		if (!bHidden)
		{
			SetEmittersVisible(false);
		}
		return;
	}
	SecondsCulled = 0.f;
	if (bHidden)
	{
		SetEmittersVisible(true);
	}

	const FSWGEffectFrame Frame = MakeFrame();
	Simulation.Update(DeltaSeconds, Frame);
	HandleChildren(Frame);
	UpdateSounds();

	for (int32 EmitterIndex = 0; EmitterIndex < Simulation.GetEmitterCount(); ++EmitterIndex)
	{
		ScratchRenderData.Reset();
		const FSWGEmitterSimulation& Emitter = Simulation.GetEmitter(EmitterIndex);
		Emitter.GatherRenderData(Frame, ScratchRenderData);
		if (Emitter.GetEmitterData().Kind == ESWGParticleKind::Mesh)
		{
			if (MeshEmitters[EmitterIndex].bSkeletal)
			{
				UpdateSkeletalEmitter(EmitterIndex, ScratchRenderData);
			}
			else
			{
				UpdateMeshEmitter(EmitterIndex, ScratchRenderData);
			}
		}
		else
		{
			UpdateQuadEmitter(EmitterIndex, ScratchRenderData);
		}
	}
}

void USWGParticleEffectComponent::UpdateQuadEmitter(int32 EmitterIndex, const TArray<FSWGParticleRenderData>& Data)
{
	UInstancedStaticMeshComponent* Instances = QuadInstances[EmitterIndex];
	if (!Instances)
	{
		return;
	}

	const int32 Needed = Data.Num();
	const int32 Current = Instances->GetInstanceCount();
	if (Current < Needed)
	{
		TArray<FTransform> Fresh;
		Fresh.Init(FTransform::Identity, Needed - Current);
		Instances->AddInstances(Fresh, false, true, false);
	}
	else if (Current > Needed)
	{
		TArray<int32> Surplus;
		for (int32 InstanceIndex = Needed; InstanceIndex < Current; ++InstanceIndex)
		{
			Surplus.Add(InstanceIndex);
		}
		Instances->RemoveInstances(Surplus);
	}
	if (Needed == 0)
	{
		return;
	}

	ScratchTransforms.SetNum(Needed);
	for (int32 InstanceIndex = 0; InstanceIndex < Needed; ++InstanceIndex)
	{
		const FSWGParticleRenderData& Particle = Data[InstanceIndex];
		const float InstanceScale = FMath::Max(2.f * FMath::Max(Particle.Width, Particle.Length), MinInstanceScale);
		ScratchTransforms[InstanceIndex] = FTransform(FQuat::Identity, SWGFx::SimToUnreal(Particle.WorldPosition), FVector(InstanceScale));
	}
	Instances->BatchUpdateInstancesTransforms(0, ScratchTransforms, true, false, true);

	float CustomData[CustomDataFloats];
	for (int32 InstanceIndex = 0; InstanceIndex < Needed; ++InstanceIndex)
	{
		const FSWGParticleRenderData& Particle = Data[InstanceIndex];
		const FVector Axis = SWGFx::SimToUnrealDirection(Particle.AlignAxis);
		CustomData[ColorR] = Particle.Color.R * PlayParams.Tint.R;
		CustomData[ColorG] = Particle.Color.G * PlayParams.Tint.G;
		CustomData[ColorB] = Particle.Color.B * PlayParams.Tint.B;
		CustomData[ColorA] = Particle.Color.A * PlayParams.Tint.A;
		CustomData[Frame] = (float)Particle.Frame;
		// Retail quad Width/Length are half-extents (RetailParticleFormat.md); the material quad spans one full size.
		CustomData[WidthCm] = 2.f * Particle.Width * SWGWorldScale;
		CustomData[LengthCm] = 2.f * Particle.Length * SWGWorldScale;
		CustomData[RotationRadians] = Particle.RotationRadians;
		CustomData[AxisX] = (float)Axis.X;
		CustomData[AxisY] = (float)Axis.Y;
		CustomData[AxisZ] = (float)Axis.Z;
		Instances->SetCustomData(InstanceIndex, MakeArrayView(CustomData, CustomDataFloats), InstanceIndex == Needed - 1);
	}
}

void USWGParticleEffectComponent::UpdateMeshEmitter(int32 EmitterIndex, const TArray<FSWGParticleRenderData>& Data)
{
	FSWGMeshEmitterRender& Emitter = MeshEmitters[EmitterIndex];
	const int32 Needed = Emitter.Mesh ? Data.Num() : 0;

	while (Emitter.Active.Num() > Needed)
	{
		Subsystem->ReleaseMeshComponent(Emitter.Active.Pop());
	}
	while (Emitter.Active.Num() < Needed)
	{
		UStaticMeshComponent* Particle = Subsystem->AcquireMeshComponent();
		Particle->SetStaticMesh(Emitter.Mesh);
		for (int32 SlotIndex = 0; SlotIndex < Emitter.Materials.Num(); ++SlotIndex)
		{
			if (Emitter.Materials[SlotIndex])
			{
				Particle->SetMaterial(SlotIndex, Emitter.Materials[SlotIndex]);
			}
		}
		Emitter.Active.Add(Particle);
	}

	for (int32 ParticleIndex = 0; ParticleIndex < Needed; ++ParticleIndex)
	{
		const FSWGParticleRenderData& Particle = Data[ParticleIndex];
		Emitter.Active[ParticleIndex]->SetWorldTransform(MakeMeshParticleTransform(Particle));
	}
}

FTransform USWGParticleEffectComponent::MakeMeshParticleTransform(const FSWGParticleRenderData& Particle)
{
	// Generated meshes face UE +X; flocks fly nose-first along their velocity, then the PTMH rotation curves apply.
	const FVector Heading = SWGFx::SimToUnrealDirection(Particle.WorldVelocity).GetSafeNormal();
	const FQuat Facing = Heading.IsNearlyZero() ? FQuat::Identity : FQuat::FindBetweenNormals(FVector::ForwardVector, Heading);
	const FQuat Orientation = Facing * SWGFx::SimToUnrealRotation(Particle.MeshRotation);
	const float MeshScale = Particle.Width > UE_KINDA_SMALL_NUMBER ? Particle.Width : 1.f;
	return FTransform(Orientation, SWGFx::SimToUnreal(Particle.WorldPosition), FVector(MeshScale));
}

void USWGParticleEffectComponent::UpdateSkeletalEmitter(int32 EmitterIndex, const TArray<FSWGParticleRenderData>& Data)
{
	FSWGMeshEmitterRender& Emitter = MeshEmitters[EmitterIndex];
	const int32 Wanted = Data.Num();

	while (Emitter.Skeletal.Num() > Wanted)
	{
		Subsystem->ReleaseSkeletalComponent(Emitter.Skeletal.Pop().Mesh);
	}
	while (Emitter.Skeletal.Num() < Wanted)
	{
		USkeletalMeshComponent* Mesh = Subsystem->AcquireSkeletalComponent();
		if (!Mesh)
		{
			break;
		}
		Emitter.Skeletal.AddDefaulted_GetRef().Mesh = Mesh;

		TWeakObjectPtr<USWGParticleEffectComponent> WeakThis(this);
		const uint32 RequestSerial = Serial;
		Subsystem->RequestSkeletalParticle(Emitter.SatPath, *Mesh, [WeakThis, RequestSerial, EmitterIndex, Mesh](bool bPlaying)
		{
			if (WeakThis.IsValid() && WeakThis->Serial == RequestSerial)
			{
				WeakThis->OnSkeletalReady(EmitterIndex, Mesh, bPlaying);
			}
		});
	}

	for (int32 ParticleIndex = 0; ParticleIndex < Emitter.Skeletal.Num(); ++ParticleIndex)
	{
		const FSWGSkeletalParticleRender& Particle = Emitter.Skeletal[ParticleIndex];
		if (Particle.bReady && Particle.Mesh)
		{
			Particle.Mesh->SetWorldTransform(MakeMeshParticleTransform(Data[ParticleIndex]));
		}
	}
}

void USWGParticleEffectComponent::OnSkeletalReady(int32 EmitterIndex, USkeletalMeshComponent* Mesh, bool bPlaying)
{
	if (!MeshEmitters.IsValidIndex(EmitterIndex))
	{
		return;
	}
	for (FSWGSkeletalParticleRender& Particle : MeshEmitters[EmitterIndex].Skeletal)
	{
		if (Particle.Mesh == Mesh)
		{
			Particle.bReady = bPlaying;
			Particle.bFailed = !bPlaying;
			Mesh->SetVisibility(bPlaying && !bHidden);
			return;
		}
	}
}

void USWGParticleEffectComponent::HandleChildren(const FSWGEffectFrame& Frame)
{
	// Drop finished children, then follow the live "trail" ones along their particle.
	for (int32 ChildIndex = Children.Num() - 1; ChildIndex >= 0; --ChildIndex)
	{
		FChildLink& Link = Children[ChildIndex];
		if (!Link.Handle.IsActive())
		{
			Children.RemoveAtSwap(ChildIndex);
			continue;
		}
		if (Link.bFollow)
		{
			FVector ParticlePosition;
			if (Simulation.GetEmitter(Link.EmitterIndex).TryGetParticleWorldPosition(Link.ParticleId, Frame, ParticlePosition))
			{
				Link.Handle.Effect->SetWorldLocation(SWGFx::SimToUnreal(ParticlePosition));
			}
			else
			{
				Link.Handle.Stop(false);
				Children.RemoveAtSwap(ChildIndex);
			}
		}
	}

	for (const FSWGChildSpawnRequest& Request : Simulation.ConsumeChildRequests())
	{
		// Retail particles at 64/s ask for a child each; the cap keeps that from becoming 64 effects.
		if (PlayParams.ChildDepth >= MaxChildDepth || Children.Num() >= Subsystem->GetMaxChildrenPerEffect())
		{
			continue;
		}

		FSWGEffectPlayParams ChildParams = PlayParams;
		ChildParams.Seed = 0;
		ChildParams.ChildDepth = PlayParams.ChildDepth + 1;
		const FTransform ChildTransform(SWGFx::SimToUnrealRotation(Request.WorldRotation), SWGFx::SimToUnreal(Request.WorldPosition));
		const FSWGEffectHandle Handle = Subsystem->PlayParticle(Request.EffectPath, nullptr, NAME_None, ChildTransform, ChildParams);
		if (Handle.IsValid())
		{
			FChildLink& Link = Children.AddDefaulted_GetRef();
			Link.Handle = Handle;
			Link.EmitterIndex = Request.EmitterIndex;
			Link.ParticleId = Request.ParticleId;
			Link.bFollow = Request.bFollowParticle;
		}
	}
}

void USWGParticleEffectComponent::UpdateSounds()
{
	if (PendingSounds.IsEmpty())
	{
		return;
	}
	USWGAudioSubsystem* Audio = Subsystem->GetAudioSubsystem();
	for (int32 SoundIndex = PendingSounds.Num() - 1; SoundIndex >= 0; --SoundIndex)
	{
		if (Simulation.GetElapsedSeconds() >= PendingSounds[SoundIndex].DelaySeconds)
		{
			if (Audio)
			{
				PlayingSounds.Add(Audio->PlaySoundAttached(PendingSounds[SoundIndex].SoundPath, this));
			}
			PendingSounds.RemoveAtSwap(SoundIndex);
		}
	}
}

void USWGParticleEffectComponent::StopEffect(bool bImmediate)
{
	if (!bActive)
	{
		return;
	}
	Simulation.Stop(bImmediate);
	PendingSounds.Reset();
	for (const FSWGSoundHandle& Sound : PlayingSounds)
	{
		Sound.Stop(bImmediate ? 0.f : 0.25f);
	}
	PlayingSounds.Reset();
	for (const FChildLink& Link : Children)
	{
		Link.Handle.Stop(bImmediate);
	}
	Children.Reset();
}

void USWGParticleEffectComponent::ReleaseResources()
{
	StopEffect(true);
	if (Subsystem)
	{
		for (UInstancedStaticMeshComponent* Instances : QuadInstances)
		{
			Subsystem->ReleaseInstanceComponent(Instances);
		}
		for (FSWGMeshEmitterRender& Emitter : MeshEmitters)
		{
			for (UStaticMeshComponent* Particle : Emitter.Active)
			{
				Subsystem->ReleaseMeshComponent(Particle);
			}
			for (const FSWGSkeletalParticleRender& Particle : Emitter.Skeletal)
			{
				Subsystem->ReleaseSkeletalComponent(Particle.Mesh);
			}
		}
	}
	QuadInstances.Reset();
	MeshEmitters.Reset();
	Simulation = FSWGEffectSimulation();
	bActive = false;
	bHasAttachParent = false;
	AttachParentWatch.Reset();
	Serial = 0;
}
