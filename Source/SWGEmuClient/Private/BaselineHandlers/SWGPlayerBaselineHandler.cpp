#include "BaselineHandlers/SWGPlayerBaselineHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Player/PlayerObjectBaseline.h"
#include "Components/SWGPlayerProfileComponent.h"
#include "Components/SWGExperienceComponent.h"
#include "Components/SWGJournalComponent.h"
#include "Components/SWGForceComponent.h"
#include "Components/SWGCraftingComponent.h"
#include "Components/SWGSocialComponent.h"
#include "Components/SWGStomachComponent.h"
#include "Components/SWGSkillComponent.h"

bool FSWGPlayerBaselineHandler::CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const
{
	return true;
}

bool FSWGPlayerBaselineHandler::HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args)
{
	FSWGPacket Packet = Msg.AsPayloadPacket();
	FPlayerObjectBaseline Baseline;

	switch (Msg.BaselineType)
	{
		// Slots 3 and 6 arrive for every player in range, so the component is
		// created on demand — a remote player is a plain ASWGCreature.
		case 3:
			SWGPlayerBaselineParser::ParseBase3(Packet, Baseline);
			USWGPlayerProfileComponent::FindOrAdd(Actor)->ApplyBase3(Baseline);
			break;

		case 6:
			SWGPlayerBaselineParser::ParseBase6(Packet, Baseline);
			USWGPlayerProfileComponent::FindOrAdd(Actor)->ApplyBase6(Baseline);
			break;

		case 8:
			SWGPlayerBaselineParser::ParseBase8(Packet, Baseline);

			if (USWGExperienceComponent* Experience = Actor.GetComponentByClass<USWGExperienceComponent>())
			{
				Experience->ApplyBase8(Baseline);
			}
			if (USWGJournalComponent* Journal = Actor.GetComponentByClass<USWGJournalComponent>())
			{
				Journal->ApplyBase8(Baseline);
			}
			if (USWGForceComponent* Force = Actor.GetComponentByClass<USWGForceComponent>())
			{
				Force->ApplyBase8(Baseline);
			}
			break;

		case 9:
			SWGPlayerBaselineParser::ParseBase9(Packet, Baseline);

			if (USWGSkillComponent* Skills = Actor.GetComponentByClass<USWGSkillComponent>())
			{
				Skills->ApplyBase9(Baseline);
			}
			if (USWGCraftingComponent* Crafting = Actor.GetComponentByClass<USWGCraftingComponent>())
			{
				Crafting->ApplyBase9(Baseline);
			}
			if (USWGSocialComponent* Social = Actor.GetComponentByClass<USWGSocialComponent>())
			{
				Social->ApplyBase9(Baseline);
			}
			if (USWGStomachComponent* Stomach = Actor.GetComponentByClass<USWGStomachComponent>())
			{
				Stomach->ApplyBase9(Baseline);
			}
			if (USWGForceComponent* Force = Actor.GetComponentByClass<USWGForceComponent>())
			{
				Force->ApplyBase9(Baseline);
			}
			break;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGPlayerBaselineHandler: no PLAY baseline dispatch for slot %d"), Msg.BaselineType);
			break;
	}

	return true;
}

REGISTER_SWG_BASELINE_HANDLER(FSWGPlayerBaselineHandler, ESWGObjectType::PLAY)
