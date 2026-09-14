#include "BaselineHandlers/SWGInstallationBaselineHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Installation/InstallationObjectBaseline.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"
#include "Components/SWGInstallationComponent.h"

bool FSWGInstallationBaselineHandler::CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const
{
	return true;
}

bool FSWGInstallationBaselineHandler::HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args)
{
	FSWGPacket Packet = Msg.AsPayloadPacket();
	FInstallationObjectBaseline Baseline;

	switch (Msg.BaselineType)
	{
		case 3:
			// HINO writes its own base3 with a different tail to INSO's.
			if (Msg.GetObjectType() == ESWGObjectType::HINO)
			{
				SWGInstallationBaselineParser::ParseHarvesterBase3(Packet, Baseline);
			}
			else
			{
				SWGInstallationBaselineParser::ParseBase3(Packet, Baseline);
			}

			if (USWGTangibleComponent* Tangible = Actor.GetComponentByClass<USWGTangibleComponent>())
			{
				Tangible->ApplyBase3(Baseline.Tangible);
			}
			if (USWGConditionComponent* Condition = Actor.GetComponentByClass<USWGConditionComponent>())
			{
				Condition->ApplyBase3(Baseline.Tangible);
			}
			if (USWGInstallationComponent* Installation = Actor.GetComponentByClass<USWGInstallationComponent>())
			{
				Installation->ApplyBase3(Baseline);
			}
			break;

		case 6:
			SWGInstallationBaselineParser::ParseBase6(Packet, Baseline);

			if (USWGDefenderComponent* Defender = Actor.GetComponentByClass<USWGDefenderComponent>())
			{
				Defender->ApplyBase6(Baseline.Tangible);
			}
			break;

		case 7:
			SWGInstallationBaselineParser::ParseBase7(Packet, Baseline);

			if (USWGHarvesterComponent* Harvester = Actor.GetComponentByClass<USWGHarvesterComponent>())
			{
				Harvester->ApplyBase7(Baseline);
			}
			break;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGInstallationBaselineHandler: no INSO baseline dispatch for slot %d"), Msg.BaselineType);
			break;
	}

	return true;
}

REGISTER_SWG_BASELINE_HANDLER(FSWGInstallationBaselineHandler,
	ESWGObjectType::INSO, ESWGObjectType::HINO)
