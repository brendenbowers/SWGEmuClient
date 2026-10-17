#include "Components/SWGAppearanceEffectsComponent.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SWGAppearanceBillboardComponent.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGWorldParticlesSubsystem.h"
#include "TRE/SWGSpriteReader.h"

USWGAppearanceEffectsComponent* USWGAppearanceEffectsComponent::Create(AActor& Owner, USceneComponent& MeshComponent, const TArray<FSWGMeshEffectPart>& Parts)
{
	if (Parts.IsEmpty() || !Owner.GetWorld() || !Owner.GetWorld()->IsGameWorld())
	{
		return nullptr;
	}
	USWGAppearanceEffectsComponent* Effects = NewObject<USWGAppearanceEffectsComponent>(&Owner);
	Effects->RegisterComponent();
	Effects->Build(MeshComponent, Parts);
	return Effects;
}

void USWGAppearanceEffectsComponent::Build(USceneComponent& MeshComponent, const TArray<FSWGMeshEffectPart>& Parts)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	USWGWorldParticlesSubsystem* Particles = World ? World->GetSubsystem<USWGWorldParticlesSubsystem>() : nullptr;
	if (!Owner || !Tre)
	{
		return;
	}

	for (const FSWGMeshEffectPart& Part : Parts)
	{
		const FTransform Relative(Part.Rotation, Part.Translation);
		if (Part.Path.EndsWith(TEXT(".prt")))
		{
			if (Particles)
			{
				// The mesh component is the actor's root, so the emitter follows the actor through this offset.
				EmitterIds.Add(Particles->AddEmitter(ESWGWorldEmitterKind::Placed, Part.Path, Relative * MeshComponent.GetComponentTransform(), Owner, &Relative));
			}
			continue;
		}

		FSWGSprite Sprite;
		if (!FSWGSpriteReader::Read(Tre->CreateIffReader(Part.Path), Sprite))
		{
			UE_LOG(LogTemp, Verbose, TEXT("USWGAppearanceEffectsComponent: sprite %s unreadable"), *Part.Path);
			continue;
		}
		USWGAppearanceBillboardComponent* Billboard = NewObject<USWGAppearanceBillboardComponent>(Owner);
		Billboard->SetupAttachment(&MeshComponent);
		Billboard->SetRelativeTransform(Relative);
		Billboard->RegisterComponent();
		if (Billboard->SetupSprite(Sprite))
		{
			Sprites.Add(Billboard);
		}
		else
		{
			Billboard->DestroyComponent();
		}
	}
}

void USWGAppearanceEffectsComponent::ReleaseEmitters()
{
	if (USWGWorldParticlesSubsystem* Particles = GetWorld() ? GetWorld()->GetSubsystem<USWGWorldParticlesSubsystem>() : nullptr)
	{
		for (const int32 EmitterId : EmitterIds)
		{
			Particles->RemoveEmitter(EmitterId);
		}
	}
	EmitterIds.Reset();
}

void USWGAppearanceEffectsComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ReleaseEmitters();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
