#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Flow/SWGClientState.h"
#include "Flow/SWGFlowContext.h"
#include "Flow/SWGFlowState.h"
#include "SWGClientFlowSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGMessageWaitSubsystem;

UCLASS()
class SWGEMU_API USWGClientFlowSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void    Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool    IsTickable() const override;

	/**
	* Registers a state for the given StateType.
	* If the state must only follow from a previous state, the PreviousState can be set.
	*/
	void RegisterState(ESWGClientState StateType, TSharedPtr<ISWGFlowState> State, ESWGClientState PreviousState = ESWGClientState::None);

	// ── Delegates ─────────────────────────────────────────────────

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStateChanged, ESWGClientState, OldState, ESWGClientState, NewState);
	UPROPERTY(BlueprintAssignable) FOnStateChanged OnStateChanged;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStatus, FText, Status);
	UPROPERTY(BlueprintAssignable) FOnStatus OnStatus;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnError, FText, Error);
	UPROPERTY(BlueprintAssignable) FOnError OnError;

	// ── BP Input API ──────────────────────────────────────────────

	UFUNCTION(BlueprintCallable) void BeginLogin(const FString& Host, const FString& Username, const FString& Password);
	UFUNCTION(BlueprintCallable) void SelectGalaxy(int32 GalaxyID);
	UFUNCTION(BlueprintCallable) void SelectCharacter(int64 CharacterID);
	UFUNCTION(BlueprintCallable) void Retry();
	UFUNCTION(BlueprintCallable) void CancelToLogin();

	// ── BP Getters ────────────────────────────────────────────────

	UFUNCTION(BlueprintPure) ESWGClientState        GetState()      const { return CurrentState; }
	UFUNCTION(BlueprintPure) TArray<FSWGGalaxyInfo>    GetGalaxies()   const { return Context.Galaxies; }
	UFUNCTION(BlueprintPure) TArray<FSWGCharacterInfo> GetCharacters() const { return Context.Characters; }
	UFUNCTION(BlueprintPure) FText                  GetStatusText() const { return Context.StatusText; }

	// ── Internal ─────────────────────────────────────────────────

	void TransitionTo(ESWGClientState NewState);
	void Fail(const FString& Reason);

	int32            Epoch = 0;
	FSWGFlowContext  Context;

	UPROPERTY() TObjectPtr<USWGNetworkSubsystem>     Network;
	UPROPERTY() TObjectPtr<USWGMessageWaitSubsystem> WaitSubsystem;

private:
	uint32 GetStateHash(const ESWGClientState First, const ESWGClientState Second);

	ESWGClientState              CurrentState = ESWGClientState::Disconnected;
	TSharedPtr<ISWGFlowState>    ActiveState;
	TMap<uint32, TSharedPtr<ISWGFlowState>> Registry;
};
