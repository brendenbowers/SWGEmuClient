#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGMessageWaitSubsystem.h"
#include "Flow/States/SWGDisconnectedState.h"
#include "Flow/States/SWGConnectingToLoginState.h"
#include "Flow/States/SWGAuthenticatingState.h"
#include "Flow/States/SWGGalaxySelectState.h"
#include "Flow/States/SWGCharacterSelectState.h"
#include "Flow/States/SWGConnectingToZoneState.h"
#include "Flow/States/SWGZoneLoadingState.h"
#include "Flow/States/SWGInWorldState.h"
#include "Flow/States/SWGErrorState.h"

void USWGClientFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Network       = Cast<USWGNetworkSubsystem>(Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));
	WaitSubsystem = Cast<USWGMessageWaitSubsystem>(Collection.InitializeDependency(USWGMessageWaitSubsystem::StaticClass()));
}

void USWGClientFlowSubsystem::Deinitialize()
{
	Epoch++;
	ActiveState.Reset();
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
	Registry[hash] = State;
}

void USWGClientFlowSubsystem::TransitionTo(ESWGClientState NewState)
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

	ActiveState->Enter(*this, Context);
	OnStateChanged.Broadcast(OldState, NewState);
}

void USWGClientFlowSubsystem::Fail(const FString& Reason)
{
	Context.ErrorText = FText::FromString(Reason);
	TransitionTo(ESWGClientState::Error);
	OnError.Broadcast(Context.ErrorText);
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
void USWGClientFlowSubsystem::SelectGalaxy(int32 GalaxyID) {}
void USWGClientFlowSubsystem::SelectCharacter(int64 CharacterID) {}
void USWGClientFlowSubsystem::Retry() {}
void USWGClientFlowSubsystem::CancelToLogin() {}
