#include "DeltaHandlers/SWGInstallationDeltaHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Installation/InstallationObjectDelta.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"
#include "Components/SWGInstallationComponent.h"

bool FSWGInstallationDeltaHandler::CanHandleDelta(const AActor& Actor, const FDeltasMessage& Msg) const
{
	return true;
}

bool FSWGInstallationDeltaHandler::HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args)
{
	FSWGPacket Packet = Msg.AsPayloadPacket();
	FInstallationObjectDelta Delta;

	switch (Msg.DeltaType)
	{
		case 3:
			SWGInstallationDeltaParser::ParseDelta3(Packet, Delta, Msg.UpdateCount);

			if (USWGTangibleComponent* Tangible = Actor.GetComponentByClass<USWGTangibleComponent>())
			{
				Tangible->ApplyDelta3(Delta.Tangible);
			}
			if (USWGConditionComponent* Condition = Actor.GetComponentByClass<USWGConditionComponent>())
			{
				Condition->ApplyDelta3(Delta.Tangible);
			}
			if (USWGInstallationComponent* Installation = Actor.GetComponentByClass<USWGInstallationComponent>())
			{
				Installation->ApplyDelta3(Delta);
			}
			break;

		case 6:
			SWGInstallationDeltaParser::ParseDelta6(Packet, Delta, Msg.UpdateCount);

			if (USWGDefenderComponent* Defender = Actor.GetComponentByClass<USWGDefenderComponent>())
			{
				Defender->ApplyDelta6(Delta.Tangible);
			}
			break;

		case 7:
			SWGInstallationDeltaParser::ParseDelta7(Packet, Delta, Msg.UpdateCount);

			if (USWGHarvesterComponent* Harvester = Actor.GetComponentByClass<USWGHarvesterComponent>())
			{
				Harvester->ApplyDelta7(Delta);
			}
			break;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGInstallationDeltaHandler: no INSO delta dispatch for slot %d"), Msg.DeltaType);
			break;
	}

	return true;
}

REGISTER_SWG_DELTA_HANDLER(FSWGInstallationDeltaHandler,
	ESWGObjectType::INSO, ESWGObjectType::HINO)
