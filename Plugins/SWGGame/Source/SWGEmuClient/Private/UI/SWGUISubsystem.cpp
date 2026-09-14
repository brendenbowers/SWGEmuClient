#include "UI/SWGUISubsystem.h"
#include "UI/SWGUISettings.h"
#include "UI/SWGGameLayout.h"
#include "UI/SWGStateTransitionConfig.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

void USWGUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (USWGClientFlowSubsystem* Flow = GetLocalPlayer()->GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		Flow->OnStateChanged.AddDynamic(this, &USWGUISubsystem::HandleStateChanged);
	}
}

void USWGUISubsystem::Deinitialize()
{
	if (UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr)
	{
		if (USWGClientFlowSubsystem* Flow = GameInstance->GetSubsystem<USWGClientFlowSubsystem>())
		{
			Flow->OnStateChanged.RemoveAll(this);
		}
	}

	Super::Deinitialize();
}

void USWGUISubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);

	// Runs from SetPlayer() during SpawnPlayActor, so the layout exists before
	// the controller's BeginPlay kicks off the flow's first transition.
	if (NewPlayerController)
	{
		EnsureLayout();
	}
}

USWGGameLayout* USWGUISubsystem::EnsureLayout()
{
	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	if (!PlayerController)
	{
		return USWGGameLayout::GetLayout(nullptr);
	}

	TSubclassOf<USWGGameLayout> LayoutClass = USWGUISettings::Get().LayoutClass.LoadSynchronous();
	if (!LayoutClass)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGUISubsystem: no LayoutClass set in Project Settings > SWG UI"));
		return nullptr;
	}

	bool bCreated = false;
	return USWGGameLayout::GetOrCreate(PlayerController, LayoutClass, bCreated);
}

void USWGUISubsystem::HandleStateChanged(ESWGClientState OldState, ESWGClientState NewState)
{
	USWGGameLayout* Layout = EnsureLayout();
	if (!Layout)
	{
		return;
	}

	// A scene change from in world (teleport, zone travel) wants the same
	// loading screen as the one from character select.
	const ESWGClientState RowOldState = (OldState == ESWGClientState::InWorld && NewState == ESWGClientState::ZoneLoading)
		? ESWGClientState::CharacterSelected : OldState;

	if (const UDataTable* Table = USWGUISettings::Get().StateTransitionTable.LoadSynchronous())
	{
		for (const auto& Row : Table->GetRowMap())
		{
			const FSWGStateTransitionRow* TransitionRow = reinterpret_cast<const FSWGStateTransitionRow*>(Row.Value);
			if (TransitionRow && TransitionRow->OldState == RowOldState && TransitionRow->NewState == NewState)
			{
				const FGameplayTag Tag = TransitionRow->LayerTag.IsValid() ? TransitionRow->LayerTag : USWGGameLayout::TAG_Layer_Menu;
				Layout->PushWidgetToLayerStack(Tag, TransitionRow->WidgetClass);
				break;
			}
		}
	}

	if (NewState == ESWGClientState::CharacterSelected)
	{
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Menu);
	}
	else if (NewState == ESWGClientState::InWorld)
	{
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Menu);
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Loading);
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Modal);
	}
}
