#pragma once

#include "CoreMinimal.h"
#include "Flow/SWGFlowTypes.h"

/** Shared blackboard passed into every state's Enter/Exit/Tick. */
struct FSWGFlowContext
{
	FString Host;
	FString Username;
	FString Password;

	TArray<uint8> SessionToken;

	TArray<FSWGGalaxyInfo>    Galaxies;
	TArray<FSWGCharacterInfo> Characters;

	int32 UserID = -1;
	int32 SelectedGalaxyID      = -1;
	int64 SelectedCharacterID   = -1;

	FText StatusText;
	FText ErrorText;

	void Reset()
	{
		Host = Username = Password = FString();
		Galaxies.Reset();
		Characters.Reset();
		SelectedGalaxyID    = -1;
		SelectedCharacterID = -1;
		StatusText = ErrorText = FText::GetEmpty();
	}
};
