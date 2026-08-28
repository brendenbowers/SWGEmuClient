#include "DeltaHandlers/SWGPlayerDeltaHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Player/PlayerObjectDelta.h"
#include "Components/SWGPlayerProfileComponent.h"
#include "Components/SWGExperienceComponent.h"
#include "Components/SWGJournalComponent.h"
#include "Components/SWGForceComponent.h"
#include "Components/SWGCraftingComponent.h"
#include "Components/SWGSocialComponent.h"
#include "Components/SWGStomachComponent.h"
#include "Components/SWGSkillComponent.h"

bool FSWGPlayerDeltaHandler::CanHandleDelta(const AActor& Actor, const FDeltasMessage& Msg) const
{
	return true;
}

bool FSWGPlayerDeltaHandler::HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args)
{
	FSWGPacket Packet = Msg.AsPayloadPacket();
	FPlayerObjectDelta Delta;

	switch (Msg.DeltaType)
	{
		// Slots 3 and 6 arrive for every player in range — see the baseline handler.
		case 3:
			SWGPlayerDeltaParser::ParseDelta3(Packet, Delta, Msg.UpdateCount);
			USWGPlayerProfileComponent::FindOrAdd(Actor)->ApplyDelta3(Delta);
			break;

		case 6:
			SWGPlayerDeltaParser::ParseDelta6(Packet, Delta, Msg.UpdateCount);
			USWGPlayerProfileComponent::FindOrAdd(Actor)->ApplyDelta6(Delta);
			break;

		case 8:
			SWGPlayerDeltaParser::ParseDelta8(Packet, Delta, Msg.UpdateCount);

			if (USWGExperienceComponent* Experience = Actor.GetComponentByClass<USWGExperienceComponent>())
			{
				Experience->ApplyDelta8(Delta);
			}
			if (USWGJournalComponent* Journal = Actor.GetComponentByClass<USWGJournalComponent>())
			{
				Journal->ApplyDelta8(Delta);
			}
			if (USWGForceComponent* Force = Actor.GetComponentByClass<USWGForceComponent>())
			{
				Force->ApplyDelta8(Delta);
			}
			break;

		case 9:
			SWGPlayerDeltaParser::ParseDelta9(Packet, Delta, Msg.UpdateCount);

			if (USWGSkillComponent* Skills = Actor.GetComponentByClass<USWGSkillComponent>())
			{
				Skills->ApplyDelta9(Delta);
			}
			if (USWGCraftingComponent* Crafting = Actor.GetComponentByClass<USWGCraftingComponent>())
			{
				Crafting->ApplyDelta9(Delta);
			}
			if (USWGSocialComponent* Social = Actor.GetComponentByClass<USWGSocialComponent>())
			{
				Social->ApplyDelta9(Delta);
			}
			if (USWGStomachComponent* Stomach = Actor.GetComponentByClass<USWGStomachComponent>())
			{
				Stomach->ApplyDelta9(Delta);
			}
			if (USWGForceComponent* Force = Actor.GetComponentByClass<USWGForceComponent>())
			{
				Force->ApplyDelta9(Delta);
			}
			break;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGPlayerDeltaHandler: no PLAY delta dispatch for slot %d"), Msg.DeltaType);
			break;
	}

	return true;
}

REGISTER_SWG_DELTA_HANDLER(FSWGPlayerDeltaHandler, ESWGObjectType::PLAY)
