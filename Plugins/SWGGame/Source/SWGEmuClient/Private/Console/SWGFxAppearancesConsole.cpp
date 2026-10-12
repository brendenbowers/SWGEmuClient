#include "Components/SWGAppearanceEffectsComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "SWGAppearanceBillboardComponent.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGBeamReader.h"
#include "TRE/SWGMeshReader.h"
#include "TRE/SWGSpriteReader.h"

#if !UE_BUILD_SHIPPING

namespace
{
	// From the editor's console the world is the editor's; use the running game's instead.
	UWorld* FindGameWorld(UWorld* World)
	{
		if (World && World->IsGameWorld())
		{
			return World;
		}
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}

	/** An empty actor with a root, three metres ahead of the player's camera. */
	AActor* SpawnTestActor(UWorld& World)
	{
		const APlayerController* Controller = World.GetFirstPlayerController();
		FVector ViewLocation;
		FRotator ViewRotation;
		if (!Controller)
		{
			return nullptr;
		}
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		AActor* Actor = World.SpawnActor<AActor>(ViewLocation + ViewRotation.Vector() * 300.f, FRotator(0.f, ViewRotation.Yaw, 0.f));
		USceneComponent* Root = NewObject<USceneComponent>(Actor);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetLifeSpan(60.f);
		return Actor;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFxAppearancesCommand(
	TEXT("swg.Fx.Appearances"),
	TEXT("swg.Fx.Appearances - counts of live sprite/beam billboards and cmp effect parts. "
		"swg.Fx.Appearances sprite <spr> | beam <beam iff> | parts <cmp> spawns one 3 m ahead for 60 s (parts lists the .prt/.spr parts and spawns them)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = FindGameWorld(InWorld);
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
		if (!World || !Tre)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Fx.Appearances: no running game"));
			return;
		}

		if (Args.Num() < 2)
		{
			int32 Billboards = 0, Beams = 0, Components = 0, Emitters = 0;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				TInlineComponentArray<USWGAppearanceBillboardComponent*> Found(*It);
				for (const USWGAppearanceBillboardComponent* Billboard : Found)
				{
					Beams += Billboard->IsBeam() ? 1 : 0;
					Billboards += 1;
				}
				if (const USWGAppearanceEffectsComponent* Effects = It->FindComponentByClass<USWGAppearanceEffectsComponent>())
				{
					++Components;
					Emitters += Effects->GetEmitterCount();
				}
			}
			UE_LOG(LogTemp, Display, TEXT("swg.Fx.Appearances: %d billboard component(s) (%d beams), %d actor(s) with cmp effect parts, %d cmp particle emitter(s)"), Billboards, Beams, Components, Emitters);
			return;
		}

		const FString Kind = Args[0].ToLower();
		FString Path = Args[1];
		AActor* Actor = nullptr;
		if (Kind == TEXT("sprite") || Kind == TEXT("beam"))
		{
			Actor = SpawnTestActor(*World);
			if (!Actor)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.Fx.Appearances: needs a player controller"));
				return;
			}
			USWGAppearanceBillboardComponent* Billboard = NewObject<USWGAppearanceBillboardComponent>(Actor);
			Billboard->SetupAttachment(Actor->GetRootComponent());
			Billboard->RegisterComponent();
			bool bOk = false;
			if (Kind == TEXT("sprite"))
			{
				FSWGSprite Sprite;
				bOk = FSWGSpriteReader::Read(Tre->CreateIffReader(Path), Sprite) && Billboard->SetupSprite(Sprite);
			}
			else
			{
				FSWGBeam Beam;
				bOk = FSWGBeamReader::Read(Tre->CreateIffReader(Path), Beam) && Billboard->SetupBeam(Beam);
			}
			UE_LOG(LogTemp, Display, TEXT("swg.Fx.Appearances: %s %s %s"), *Kind, *Path, bOk ? TEXT("spawned") : TEXT("FAILED"));
			if (!bOk)
			{
				Actor->Destroy();
			}
			return;
		}
		if (Kind == TEXT("parts"))
		{
			FSWGMeshData Mesh;
			FSWGMeshReader::ReadComponentMesh(Tre->CreateIffReader(Path), [](const FString&, FSWGMeshData&) { return false; }, Mesh);
			for (const FSWGMeshEffectPart& Part : Mesh.EffectParts)
			{
				UE_LOG(LogTemp, Display, TEXT("swg.Fx.Appearances: %s part %s at %s"), *Path, *Part.Path, *Part.Translation.ToString());
			}
			Actor = SpawnTestActor(*World);
			if (Actor && USWGAppearanceEffectsComponent::Create(*Actor, *Actor->GetRootComponent(), Mesh.EffectParts))
			{
				UE_LOG(LogTemp, Display, TEXT("swg.Fx.Appearances: spawned %d part(s)"), Mesh.EffectParts.Num());
			}
			return;
		}
		UE_LOG(LogTemp, Warning, TEXT("swg.Fx.Appearances: unknown kind '%s'"), *Kind);
	}));

#endif // !UE_BUILD_SHIPPING
