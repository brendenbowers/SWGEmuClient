#include "SpawnHandlers/SWGTerminalSpawnHandler.h"
#include "Objects/Tangible/SWGItem.h"
#include "Objects/SWGObject.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Engine/GameInstance.h"

REGISTER_SWG_ACTOR_SPAWN_HANDLER(FSWGTerminalSpawnHandler, ASWGItem)

namespace
{
	// server/zone/objects/scene/SceneObjectType.h — retail's own object-type
	// enum, baked into every SharedObjectTemplate as its gameObjectType field.
	// The whole terminal family lives in 0x4000-0x40xx; only the ones anything
	// here acts on get their own case.
	constexpr int32 GOT_TerminalFamilyMask = 0xFF00;
	constexpr int32 GOT_TerminalFamily     = 0x4000;
	constexpr int32 GOT_Bank               = 0x4001;
	constexpr int32 GOT_Bazaar             = 0x4002;
	constexpr int32 GOT_MissionTerminal    = 0x4006;
	constexpr int32 GOT_TravelTerminal     = 0x4012;
}

bool FSWGTerminalSpawnHandler::ClassifyGameObjectType(int32 GameObjectType, ESWGTerminalType& OutType)
{
	if ((GameObjectType & GOT_TerminalFamilyMask) != GOT_TerminalFamily)
	{
		return false;
	}

	switch (GameObjectType)
	{
		case GOT_MissionTerminal: OutType = ESWGTerminalType::Mission; break;
		case GOT_TravelTerminal:  OutType = ESWGTerminalType::Travel;  break;
		case GOT_Bazaar:          OutType = ESWGTerminalType::Bazaar;  break;
		case GOT_Bank:            OutType = ESWGTerminalType::Bank;    break;
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

	int32 GameObjectType = 0;
	if (!TreSubsystem || TemplatePath.IsEmpty() || !TreSubsystem->FindTemplateIntParam(TemplatePath, TEXT("gameObjectType"), GameObjectType))
	{
		return false;
	}

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
