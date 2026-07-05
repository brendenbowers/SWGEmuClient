#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "SWGClientState.generated.h"

UENUM(BlueprintType)
enum class ESWGClientState : uint8
{
	None, // special case for transition rules
	Initialization, // runs once at boot, before the connection screen — TRE readiness etc.
	Disconnected,
	ConnectingToLogin,
	Authenticating,
	GalaxySelect,
	GalaxySelected,
	CharacterSelect,
	CharacterSelected,
	ConnectingToZone,
	ZoneLoading,
	InWorld,
	Error
};

inline FString LexToString(ESWGClientState State)
{
	switch (State)
	{
		case ESWGClientState::Initialization:      return TEXT("Initialization");
		case ESWGClientState::Disconnected:        return TEXT("Disconnected");
		case ESWGClientState::ConnectingToLogin:   return TEXT("ConnectingToLogin");
		case ESWGClientState::Authenticating:      return TEXT("Authenticating");
		case ESWGClientState::GalaxySelect:        return TEXT("GalaxySelect");
		case ESWGClientState::GalaxySelected:      return TEXT("GalaxySelected");
		case ESWGClientState::CharacterSelect:     return TEXT("CharacterSelect");
		case ESWGClientState::CharacterSelected:   return TEXT("CharacterSelected");
		case ESWGClientState::ConnectingToZone:    return TEXT("ConnectingToZone");
		case ESWGClientState::ZoneLoading:         return TEXT("ZoneLoading");
		case ESWGClientState::InWorld:             return TEXT("InWorld");
		case ESWGClientState::Error:               return TEXT("Error");
		default:                                   return TEXT("Unknown");
	}
}

inline FText LexToText(ESWGClientState State)
{
	switch (State)
	{
		case ESWGClientState::Initialization:      return INVTEXT("Initialization");
		case ESWGClientState::Disconnected:        return INVTEXT("Disconnected");
		case ESWGClientState::ConnectingToLogin:   return INVTEXT("ConnectingToLogin");
		case ESWGClientState::Authenticating:      return INVTEXT("Authenticating");
		case ESWGClientState::GalaxySelect:        return INVTEXT("GalaxySelect");
		case ESWGClientState::GalaxySelected:      return INVTEXT("GalaxySelected");
		case ESWGClientState::CharacterSelect:     return INVTEXT("CharacterSelect");
		case ESWGClientState::CharacterSelected:   return INVTEXT("CharacterSelected");
		case ESWGClientState::ConnectingToZone:    return INVTEXT("ConnectingToZone");
		case ESWGClientState::ZoneLoading:         return INVTEXT("ZoneLoading");
		case ESWGClientState::InWorld:             return INVTEXT("InWorld");
		case ESWGClientState::Error:               return INVTEXT("Error");
		default:                                   return INVTEXT("Unknown");
	}
}
