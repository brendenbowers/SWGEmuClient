#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGMessageWaitSubsystem.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Flow/SWGGalaxySelectedPayload.h"
#include "Flow/SWGCharacterSelectedPayload.h"
#include "Flow/SWGStateTransitionConfig.h"
#include "UI/SWGGameLayout.h"

void USWGClientFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Network       = Cast<USWGNetworkSubsystem>(Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));
	WaitSubsystem = Cast<USWGMessageWaitSubsystem>(Collection.InitializeDependency(USWGMessageWaitSubsystem::StaticClass()));

	OnStateChanged.AddDynamic(this, &USWGClientFlowSubsystem::HandleStateChanged);

	FSWGFlowStateRegistry::Get().RegisterAllStates(*this);
}

void USWGClientFlowSubsystem::Deinitialize()
{
	Epoch++;
	ActiveState.Reset();

	OnStateChanged.RemoveAll(this);
	
	Super::Deinitialize();
}

void USWGClientFlowSubsystem::Tick(float DeltaTime)
{
	if (ActiveState)
	{
		ActiveState->Tick(*this, Context, DeltaTime);
	}
}

TStatId USWGClientFlowSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGClientFlowSubsystem, STATGROUP_Tickables);
}

bool USWGClientFlowSubsystem::IsTickable() const
{
	return ActiveState != nullptr;
}

void USWGClientFlowSubsystem::RegisterState(ESWGClientState StateType, TSharedPtr<ISWGFlowState> State, ESWGClientState PreviousState)
{
	uint32 hash = GetStateHash(StateType, PreviousState);
	Registry.Add(hash, State);
}

void USWGClientFlowSubsystem::TransitionTo(ESWGClientState NewState, TSharedPtr<FSWGTransitionPayload> Payload)
{
	const ESWGClientState OldState = CurrentState;
	if (ActiveState)
	{
		ActiveState->Exit(*this, Context);
	}
	Epoch++;

	TSharedPtr<ISWGFlowState> NextState;
	if (TSharedPtr<ISWGFlowState>* Constrained = Registry.Find(GetStateHash(NewState, OldState)))
	{
		NextState = *Constrained;
	}
	else if (TSharedPtr<ISWGFlowState>* Fallback = Registry.Find(GetStateHash(NewState, ESWGClientState::None)))
	{
		NextState = *Fallback;
	}

	if (NextState)
	{
		CurrentState = NewState;
		ActiveState = NextState;
	}
	else 
	{
		UE_LOG(LogTemp, Warning, TEXT("No valid state registered"));
		ActiveState.Reset();
		return;
	}

	ActiveState->Enter(*this, Context, Payload);
	OnStateChanged.Broadcast(OldState, NewState);
}

void USWGClientFlowSubsystem::Fail(const FString& Reason)
{
	Context.ErrorText = FText::FromString(Reason);
	TransitionTo(ESWGClientState::Error);
	OnError.Broadcast(Context.ErrorText);
}

void USWGClientFlowSubsystem::HandleStateChanged(ESWGClientState OldState, ESWGClientState NewState)
{
	if (!StateTransitionTable)
	{
		return;
	}

	for (auto& Row : StateTransitionTable->GetRowMap())
	{
		FSWGStateTransitionRow* TransitionRow = (FSWGStateTransitionRow*)Row.Value;
		if (TransitionRow && TransitionRow->OldState == OldState && TransitionRow->NewState == NewState)
		{
			if (USWGGameLayout* Layout = USWGGameLayout::GetLayout(GetWorld()))
			{
				FGameplayTag Tag = USWGGameLayout::TAG_Layer_Menu;
				if (TransitionRow->LayerTag != FGameplayTag::EmptyTag)
				{
					Tag = TransitionRow->LayerTag;
				}

				Layout->PushWidgetToLayerStack(Tag, TransitionRow->WidgetClass);
				break;
			}
		}
	}
}

uint32 USWGClientFlowSubsystem::GetStateHash(const ESWGClientState First, const ESWGClientState Second)
{
	return HashCombine(GetTypeHash(static_cast<uint32>(First)), GetTypeHash(static_cast<uint32>(Second)));
}

void USWGClientFlowSubsystem::BeginLogin(const FString& Host, const FString& Username, const FString& Password) 
{
	if (CurrentState != ESWGClientState::Disconnected)
		return;

	Context.Host = Host;
	Context.Username = Username;
	Context.Password = Password;

	TransitionTo(ESWGClientState::ConnectingToLogin);
}
void USWGClientFlowSubsystem::SelectGalaxy(int32 GalaxyID)
{
	if (CurrentState != ESWGClientState::GalaxySelect)
		return;

	TSharedPtr<FSWGTransitionPayload> Payload = MakeShared<FSWGGalaxySelectedPayload>(GalaxyID);
	TransitionTo(ESWGClientState::GalaxySelected, Payload);
}
void USWGClientFlowSubsystem::SelectCharacter(int64 CharacterID)
{
	if (CurrentState != ESWGClientState::CharacterSelect)
		return;

	TSharedPtr<FSWGTransitionPayload> Payload = MakeShared<FSWGCharacterSelectedPayload>(CharacterID);
	TransitionTo(ESWGClientState::CharacterSelected, Payload);
}
void USWGClientFlowSubsystem::Retry() {}
void USWGClientFlowSubsystem::CancelToLogin() {}
