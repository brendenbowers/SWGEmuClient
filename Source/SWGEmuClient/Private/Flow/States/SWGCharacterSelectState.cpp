#include "Flow/States/SWGCharacterSelectState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Objects/Player/SWGPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/TargetPoint.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Components/MeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"

namespace
{
	/**
	 * Keeps the spawned preview character sitting on the spawn point.
	 * UCharacterSelectWidget re-derives that point whenever the viewport
	 * resizes, but the character is only spawned on a selection change - so
	 * without this it stays where the panel used to be and slides out from
	 * under its own name label. Looked up rather than cached so nothing holds a
	 * stale actor across respawns.
	 */
	void SyncPreviewCharacterToSpawnPoint(UWorld& World)
	{
		TArray<AActor*> SpawnPoints;
		UGameplayStatics::GetAllActorsOfClassWithTag(&World, ATargetPoint::StaticClass(), TEXT("CharacterPreviewSpawnPoint"), SpawnPoints);
		if (SpawnPoints.IsEmpty())
		{
			return;
		}

		TArray<AActor*> Characters;
		UGameplayStatics::GetAllActorsOfClass(&World, ASWGPlayer::StaticClass(), Characters);

		// Only the horizontal placement comes from the spawn point. The actor's
		// Z is the capsule centre the mesh pipeline settled on (the mesh
		// component carries an equal and opposite offset so the feet land on the
		// ground), so preserve it rather than snapping the character downward.
		const FVector SpawnLocation = SpawnPoints[0]->GetActorLocation();
		for (AActor* Character : Characters)
		{
			const FVector Current = Character->GetActorLocation();
			const FVector Desired(SpawnLocation.X, SpawnLocation.Y, Current.Z);
			if (!Current.Equals(Desired))
			{
				Character->SetActorLocation(Desired);
			}
		}
	}
}

void FSWGCharacterSelectState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload)
{
	UWorld* World = UIStateMachine.GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsOfClassWithTag(World, ACameraActor::StaticClass(), TEXT("CharacterPreviewCamera"), Cameras);
	if (Cameras.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCharacterSelectState: no CharacterPreviewCamera found in level"));
		return;
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		PC->SetViewTargetWithBlend(Cameras[0], 0.f);
	}
}
void FSWGCharacterSelectState::Exit(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}

void FSWGCharacterSelectState::Tick(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, float Dt)
{
	if (Ctx.SelectedCharacterID != SelectedCharacterID)
	{
		SelectedCharacterID = Ctx.SelectedCharacterID;
		DisplpayCharacter(UIStateMachine, Ctx);
	}

	if (UWorld* World = UIStateMachine.GetWorld())
	{
		SyncPreviewCharacterToSpawnPoint(*World);
	}
}

void FSWGCharacterSelectState::DisplpayCharacter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx)
{
	UWorld* World = UIStateMachine.GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Verbose, TEXT("FSWGCharacterSelectState: Unable to get world when trying to display character"));
		return;
	}
	TArray<AActor*> ExistingCharacters;
	UGameplayStatics::GetAllActorsOfClass(World, ASWGPlayer::StaticClass(), ExistingCharacters);
	for (int i = 0; i < ExistingCharacters.Num(); i++)
	{
		ExistingCharacters[i]->Destroy();
	}

	if (Ctx.SelectedCharacterID == -1)
	{
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Verbose, TEXT("FSWGCharacterSelectState: Unable to get game instance when trying to display character"));
		return;
	}

	TArray<AActor*> SpawnPoints;
	UGameplayStatics::GetAllActorsOfClassWithTag(World, ATargetPoint::StaticClass(), TEXT("CharacterPreviewSpawnPoint"), SpawnPoints);
	if (SpawnPoints.IsEmpty())
	{
		UE_LOG(LogTemp, Verbose, TEXT("FSWGCharacterSelectState: Unable to locate Target Point to spawn character at"));
		return;
	}


	USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>();
	if (!MeshGenerator)
	{
		UE_LOG(LogTemp, Verbose, TEXT("FSWGCharacterSelectState: Unable to get the MeshGeneratorSubsystem"));
		return;
	}

	const FTransform Transform = SpawnPoints[0]->GetTransform();
	UE_LOG(LogTemp, Warning, TEXT("FSWGCharacterSelectState::DisplpayCharacter spawning at %s (spawn point %s)"),
		*Transform.GetLocation().ToString(), *SpawnPoints[0]->GetName());


	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASWGPlayer* PlayerActor = World->SpawnActor<ASWGPlayer>(ASWGPlayer::StaticClass(), Transform, SpawnParameters);
	
	if (UCharacterMovementComponent* MovementComponent = PlayerActor->GetCharacterMovement())
	{
		MovementComponent->DisableMovement();
		MovementComponent->SetComponentTickEnabled(false);
	}
	PlayerActor->SetActorEnableCollision(false);

	FSWGCharacterInfo* CharacterInfo = Ctx.Characters.FindByPredicate([CharacterID = Ctx.SelectedCharacterID](const FSWGCharacterInfo& CharacterInfo) { return CharacterInfo.CharacterID == CharacterID; });
	if (!CharacterInfo)
	{
		UE_LOG(LogTemp, Verbose, TEXT("FSWGCharacterSelectState: Unable to find character info for selected character id %d"), Ctx.SelectedCharacterID);
		return;
	}

	// TODO: Support Cancelling the request
	// TODO: Add loading the cached character data to load the visuals for equipment
	TWeakObjectPtr<AActor> WeakPlayerActor = PlayerActor;
	MeshGenerator->RequestMesh(PlayerActor, CharacterInfo->RaceGenderCRC)
		.Next([WeakPlayerActor](FSWGMeshGenerationResult Result)
		{
			if (!WeakPlayerActor.IsValid() || !Result.MeshOrComponent.IsType<UMeshComponent*>())
			{
				return;
			}

			UMeshComponent* MeshComponent = Result.MeshOrComponent.Get<UMeshComponent*>();
			if (!MeshComponent)
			{
				return;
			}

			MeshComponent->SetCastShadow(false);
			MeshComponent->bVisibleInRayTracing = false;
		});
}

REGISTER_FLOW_STATE(FSWGCharacterSelectState, ESWGClientState::CharacterSelect)
