#include "SpawnHandlers/SWGTerminalSpawnHandler.h"
#include "Objects/Tangible/SWGItem.h"
#include "Objects/SWGObject.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Components/SWGTangibleComponent.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Network/Objects/Zone/Object/SWGGameObjectType.h"
#include "Engine/GameInstance.h"

REGISTER_SWG_ACTOR_SPAWN_HANDLER(FSWGTerminalSpawnHandler, ASWGItem)

bool FSWGTerminalSpawnHandler::ClassifyGameObjectType(int32 GameObjectType, ESWGTerminalType& OutType)
{
	if ((GameObjectType & SWGGameObjectType::TerminalFamilyMask) != SWGGameObjectType::TerminalFamily)
	{
		return false;
	}

	switch (GameObjectType)
	{
		case SWGGameObjectType::MissionTerminal: OutType = ESWGTerminalType::Mission; break;
		case SWGGameObjectType::TravelTerminal:  OutType = ESWGTerminalType::Travel;  break;
		case SWGGameObjectType::BazaarTerminal:  OutType = ESWGTerminalType::Bazaar;  break;
		case SWGGameObjectType::BankTerminal:    OutType = ESWGTerminalType::Bank;    break;
		default:                  OutType = ESWGTerminalType::Other;   break;
	}
	return true;
}

bool FSWGTerminalSpawnHandler::HandleActorSpawn(AActor& Actor, const FSWGActorSpawnArguments& SpawnInfo)
{
	FString TemplatePath = SpawnInfo.TemplateName;
	UGameInstance* GameInstance = Actor.GetWorld() ? Actor.GetWorld()->GetGameInstance() : nullptr;
	USWGTreSubsystem* TreSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	if (TemplatePath.IsEmpty() && TreSubsystem)
	{
		TemplatePath = TreSubsystem->ResolveTemplatePath(SpawnInfo.TemplateCrc);
	}

	if (TemplatePath.IsEmpty())
	{
		return false;
	}
	const int32 GameObjectType = USWGTangibleComponent::GetGameObjectType(&Actor);

	ESWGTerminalType Type = ESWGTerminalType::Other;
	// shared_terminal_travel.iff uses the client's ambiguous 0x400C type,
	// despite Core3 also defining its server-side travel type as 0x4012.
	const bool bTravelTemplate = TemplatePath.EndsWith(TEXT("/shared_terminal_travel.iff"), ESearchCase::IgnoreCase);
	if (bTravelTemplate || ClassifyGameObjectType(GameObjectType, Type))
	{
		USWGTerminalComponent* Terminal = NewObject<USWGTerminalComponent>(&Actor);
		Terminal->TerminalType = bTravelTemplate ? ESWGTerminalType::Travel : Type;
		Terminal->GameObjectType = GameObjectType;
		Terminal->RegisterComponent();
	}

	// Tagging only — every ASWGItem, terminal or not, still needs the
	// generic mesh-gen fallback the caller runs when this comes back false.
	return false;
}
