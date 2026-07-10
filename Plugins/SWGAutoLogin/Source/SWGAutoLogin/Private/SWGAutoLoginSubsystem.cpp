#include "SWGAutoLoginSubsystem.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

namespace
{
	// Hardcoded test-server credentials — this whole subsystem is a
	// throwaway PIE-testing convenience, not a real login path.
	const FString AutoLoginHost     = TEXT("localhost");
	const FString AutoLoginUsername = TEXT("test");
	const FString AutoLoginPassword = TEXT("test");
}

void USWGAutoLoginSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FlowSubsystem = Cast<USWGClientFlowSubsystem>(Collection.InitializeDependency(USWGClientFlowSubsystem::StaticClass()));
	if (FlowSubsystem)
	{
		FlowSubsystem->OnStateChanged.AddDynamic(this, &USWGAutoLoginSubsystem::HandleStateChanged);
	}
}

void USWGAutoLoginSubsystem::Deinitialize()
{
	if (FlowSubsystem)
	{
		FlowSubsystem->OnStateChanged.RemoveDynamic(this, &USWGAutoLoginSubsystem::HandleStateChanged);
	}

	Super::Deinitialize();
}

void USWGAutoLoginSubsystem::HandleStateChanged(ESWGClientState OldState, ESWGClientState NewState)
{
	if (!FlowSubsystem)
	{
		return;
	}

	// Each of these states otherwise waits on UI input (see
	// SWGDisconnectedState/SWGGalaxySelectState/SWGCharacterSelectState —
	// all passive Enter()s) — every other state in the flow already
	// auto-advances once its network step resolves.
	switch (NewState)
	{
		case ESWGClientState::Disconnected:
			FlowSubsystem->BeginLogin(AutoLoginHost, AutoLoginUsername, AutoLoginPassword);
			break;

		case ESWGClientState::GalaxySelect:
		{
			const TArray<FSWGGalaxyInfo>& Galaxies = FlowSubsystem->GetGalaxies();
			if (Galaxies.Num() > 0)
			{
				FlowSubsystem->SelectGalaxy(Galaxies[0].GalaxyID);
			}
			break;
		}

		case ESWGClientState::CharacterSelect:
		{
			const TArray<FSWGCharacterInfo>& Characters = FlowSubsystem->GetCharacters();
			if (Characters.Num() > 0)
			{
				FlowSubsystem->SelectCharacter(Characters[0].CharacterID);
			}
			break;
		}

		default:
			break;
	}
}
