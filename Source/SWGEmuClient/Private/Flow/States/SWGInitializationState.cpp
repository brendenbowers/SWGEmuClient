#include "Flow/States/SWGInitializationState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Async/Async.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGFormTagMapping.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/SWGCharacterPreviewLayout.h"
#include "SaveData/SWGCharacterPreviewSaveGame.h"


void FSWGInitializationState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload)
{
	UIStateMachine.OnStatus.Broadcast(FText::FromString(TEXT("Initializing...")));

	if (UWorld* World = UIStateMachine.GetWorld())
	{
		TArray<AActor*> Cameras;
		UGameplayStatics::GetAllActorsOfClassWithTag(World, ACameraActor::StaticClass(), TEXT("CharacterPreviewCamera"), Cameras);
		if (!Cameras.IsEmpty())
		{
			if (ACameraActor* PreviewCamera = Cast<ACameraActor>(Cameras[0]))
			{
				PreviewCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
				if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
				{
					PlayerController->SetViewTarget(PreviewCamera);
				}
			}
		}
	}

	constexpr TCHAR CharacterCacheSlot[] = TEXT("CharacterPreviews");
	USWGCharacterPreviewSaveGame* CharacterCache = Cast<USWGCharacterPreviewSaveGame>(
		UGameplayStatics::LoadGameFromSlot(CharacterCacheSlot, 0));
	if (!CharacterCache)
	{
		CharacterCache = Cast<USWGCharacterPreviewSaveGame>(UGameplayStatics::CreateSaveGameObject(
			USWGCharacterPreviewSaveGame::StaticClass()));
	}
	Ctx.CharacterCache.Reset(CharacterCache);

	if (!FormTagMappingTable)
	{
		FormTagMappingTable = LoadObject<UDataTable>(nullptr,
			TEXT("/Game/SWGEmu/Data/DT_SWGFormTagMappings.DT_SWGFormTagMappings"));
	}


	if (UGameInstance* GameInstance = UIStateMachine.GetGameInstance())
	{
		if (USWGTreSubsystem* TreSubsystem = GameInstance->GetSubsystem<USWGTreSubsystem>())
		{

			int32 Epoch = UIStateMachine.Epoch;
			TWeakObjectPtr<USWGClientFlowSubsystem> StateMchineWeakRef = &UIStateMachine;
			TWeakObjectPtr<USWGTreSubsystem> TreSubsystemWeakRef = TreSubsystem;
			TWeakObjectPtr<USWGObjectGraphSubsystem> ObjectGraphWeakRef = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>();

			Async(EAsyncExecution::ThreadPool, [this, StateMchineWeakRef, TreSubsystemWeakRef, ObjectGraphWeakRef, FormTagMappingTable = FormTagMappingTable, Epoch]()
				{
					TStrongObjectPtr<USWGClientFlowSubsystem> StateMachine = StateMchineWeakRef.Pin();
					TStrongObjectPtr<USWGTreSubsystem> TreSubsystem = TreSubsystemWeakRef.Pin();
					TStrongObjectPtr<USWGObjectGraphSubsystem> ObjectGraph = ObjectGraphWeakRef.Pin();
					if (!StateMachine.IsValid() || !TreSubsystem.IsValid() || StateMachine->Epoch != Epoch)
					{
						return;
					}

					if (!TreSubsystem->IsLoaded())
					{

						StateMachine->Status(TEXT("Loading TRE archives..."));
						if (!TreSubsystem->LoadArchives())
						{
							UE_LOG(LogTemp, Error, TEXT("SWGInitializationState: Failed to load TRE archives."));
							TPromise<void> Promise;
							TFuture<void> Future = Promise.GetFuture();
							AsyncTask(ENamedThreads::GameThread, [StateMachine, Epoch, Promise = MoveTemp(Promise)]() mutable
								{
									ON_SCOPE_EXIT
									{
										Promise.SetValue();
									};

									if (StateMachine->Epoch != Epoch)
									{
										UE_LOG(LogTemp, Warning, TEXT("SWGInitializationState: Epoch mismatch during TRE load failure handling. Expected %d, got %d"), Epoch, StateMachine->Epoch);
										return;
									}
									StateMachine->Fail(TEXT("Failed to load TRE archives."));
								});
							Future.WaitFor(FTimespan::FromSeconds(10));
							return;
						}
					}

					StateMachine->Status(TEXT("Generating Crc To Class Map"));
					TMap<uint32, TSubclassOf<AActor>> CrcToSubclass;
					for (auto& [Crc, Path] : TreSubsystem->GetCrcToTemplatePathMap())
					{
						FSWGIffReader Reader = TreSubsystem->CreateIffReader(Path);
						if (!Reader.IsValid())
						{
							// Expected for a small number (~35 of 15792) of legacy CRC table
							// entries whose template was renamed/removed in later patches
							// (mostly object/creature/player/* pre-"shared_" rename) — the
							// CRC string is kept for wire compatibility, but no archive
							// record exists under that exact name anymore. A live server
							// never sends these specific CRCs, so this is harmless noise,
							// not a sign the TRE/IFF reading is broken.
							UE_LOG(LogTemp, Verbose, TEXT("SWGInitializationState: no archive record for template %s (CRC %08X) — likely a stale/renamed legacy CRC entry"), *Path, Crc);
							continue;
						}

						FName FormType = Reader.GetRootFormType();
						if (FormType == NAME_None)
						{
							UE_LOG(LogTemp, Warning, TEXT("SWGInitializationState: Failed to get root form type for template %s"), *Path);
							continue;
						}

						FSWGFormTagMapping* Mapping = FormTagMappingTable->FindRow<FSWGFormTagMapping>(FormType, TEXT("SWGInitializationState"), false);
						if (!Mapping)
						{
							UE_LOG(LogTemp, Warning, TEXT("SWGInitializationState: No mapping found for form type %s (template %s)"), *FormType.ToString(), *Path);
							continue;
						}

						CrcToSubclass.Add(Crc, Mapping->ActorClass);
					}

					AsyncTask(ENamedThreads::GameThread, [this, StateMachine, ObjectGraph, CrcToSubclass = MoveTemp(CrcToSubclass), Epoch]()
						{
							if (ObjectGraph.IsValid())
							{
								ObjectGraph->SetCrcToActorClassMap(CrcToSubclass);
							}

							if (StateMachine.IsValid())
							{
								if (UGameInstance* GameInstance = StateMachine->GetGameInstance())
								{
									SetupPreviewBackdrop(*GameInstance);
								}
							}

							if (StateMachine.IsValid() && StateMachine->Epoch == Epoch)
							{
								StateMachine->TransitionTo(ESWGClientState::Disconnected);
							}
						});
				});
		}
	}

}

void FSWGInitializationState::SetupPreviewBackdrop(UGameInstance& GameInstance)
{
	UWorld* World = GameInstance.GetWorld();
	USWGTreSubsystem* TreSubsystem = GameInstance.GetSubsystem<USWGTreSubsystem>();
	if (!World || !TreSubsystem)
	{
		return;
	}

	TArray<AActor*> Backdrops;
	UGameplayStatics::GetAllActorsOfClassWithTag(World, AStaticMeshActor::StaticClass(), TEXT("CharacterPreviewBackdrop"), Backdrops);
	if (Backdrops.IsEmpty())
	{
		return;
	}

	UTexture2D* BackdropTexture = TreSubsystem->GetOrLoadTexture(TEXT("texture/ui_background_arrow.dds"));
	UMaterial* BackdropMaterialParent = LoadObject<UMaterial>(nullptr, TEXT("/Game/SWGEmu/UI/M_UIBackdropUnlit.M_UIBackdropUnlit"));
	if (!BackdropTexture || !BackdropMaterialParent)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGInitializationState: failed to set up preview backdrop (texture=%d, material=%d)"),
			BackdropTexture != nullptr, BackdropMaterialParent != nullptr);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("FSWGInitializationState: preview backdrop texture '%s' loaded %dx%d, format=%d, SRGB=%d, numMips=%d"),
		*BackdropTexture->GetName(), BackdropTexture->GetSizeX(), BackdropTexture->GetSizeY(),
		(int32)BackdropTexture->GetPixelFormat(), BackdropTexture->SRGB, BackdropTexture->GetPlatformData() ? BackdropTexture->GetPlatformData()->Mips.Num() : -1);

	UMaterialInstanceDynamic* BackdropMID = UMaterialInstanceDynamic::Create(BackdropMaterialParent, World);
	BackdropMID->SetTextureParameterValue(TEXT("Texture"), BackdropTexture);

	AStaticMeshActor* Backdrop = Cast<AStaticMeshActor>(Backdrops[0]);
	if (!Backdrop)
	{
		return;
	}

	if (UStaticMeshComponent* MeshComponent = Backdrop->GetStaticMeshComponent())
	{
		MeshComponent->SetMaterial(0, BackdropMID);
	}

	SWGCharacterPreview::FitBackdropToCamera(*World);
}

void FSWGInitializationState::Exit(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx)
{
}

REGISTER_FLOW_STATE(FSWGInitializationState, ESWGClientState::Initialization)
