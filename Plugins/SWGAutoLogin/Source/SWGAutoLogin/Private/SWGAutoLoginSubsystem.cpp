#include "SWGAutoLoginSubsystem.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

static int32 GSWGAutoLoginConnect = 1;
static FAutoConsoleVariableRef CVarSWGAutoLoginConnect(
	TEXT("swg.AutoLogin.Connect"),
	GSWGAutoLoginConnect,
	TEXT("Automatically connect to the auto-login server on startup. ( 0 = disabled, 1 = enabled )"),
	ECVF_Default);

static int32 GSWGAutoLoginSelectGalaxy = 0;
static FAutoConsoleVariableRef CVarSWGAutoLoginSelectGalaxy(
	TEXT("swg.AutoLogin.SelectGalaxy"),
	GSWGAutoLoginSelectGalaxy,
	TEXT("Automatically select the first available galaxy at the index on login. (-1 = disabled, 0 = first available, 1 = second available, etc.)"),
	ECVF_Default);

static int32 GSWGAutoLoginSelectCharacter = 0;
static FAutoConsoleVariableRef CVarSWGAutoLoginSelectCharacter(
	TEXT("swg.AutoLogin.SelectCharacter"),
	GSWGAutoLoginSelectCharacter,
	TEXT("Automatically select the first available character at the index on login. (-1 = disabled, 0 = first available, 1 = second available, etc.)"),
	ECVF_Default);


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
			if (GSWGAutoLoginConnect > 0)
			{
				FlowSubsystem->BeginLogin(AutoLoginHost, AutoLoginUsername, AutoLoginPassword);
			}
			break;

		case ESWGClientState::GalaxySelect:
		{
			const TArray<FSWGGalaxyInfo>& Galaxies = FlowSubsystem->GetGalaxies();
			if (Galaxies.Num() > 0 && GSWGAutoLoginSelectGalaxy >= 0 && GSWGAutoLoginSelectGalaxy < Galaxies.Num())
			{
				FlowSubsystem->SelectGalaxy(Galaxies[GSWGAutoLoginSelectGalaxy].GalaxyID);
			}
			break;
		}

		case ESWGClientState::CharacterSelect:
		{
			const TArray<FSWGCharacterInfo>& Characters = FlowSubsystem->GetCharacters();
			if (Characters.Num() > 0 && GSWGAutoLoginSelectCharacter >= 0 && GSWGAutoLoginSelectCharacter < Characters.Num())
			{
				FlowSubsystem->SelectCharacter(Characters[GSWGAutoLoginSelectCharacter].CharacterID);
			}
			break;
		}

		default:
			break;
	}
}
