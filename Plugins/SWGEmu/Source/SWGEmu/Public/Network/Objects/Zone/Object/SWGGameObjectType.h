#pragma once

#include "CoreMinimal.h"

// Core3 SceneObjectType.h values stored as gameObjectType in shared object templates.
namespace SWGGameObjectType
{
	constexpr int32 CraftingStation = 0x2006;
	constexpr int32 CraftingTool = 0x8001;
	constexpr int32 TerminalFamilyMask = 0xFF00;
	constexpr int32 TerminalFamily = 0x4000;
	constexpr int32 BankTerminal = 0x4001;
	constexpr int32 BazaarTerminal = 0x4002;
	constexpr int32 MissionTerminal = 0x4006;
	constexpr int32 TravelTerminal = 0x4012;
}
