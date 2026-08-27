#include "DeltaHandlers/SWGTangibleDeltaHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Object/TangibleObjectDelta.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"

bool FSWGTangibleDeltaHandler::CanHandleDelta(const AActor& Actor, const FDeltasMessage& Msg) const
{
	return true;
}

bool FSWGTangibleDeltaHandler::HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args)
{
	FSWGPacket Packet = Msg.AsPayloadPacket();
	FTangibleObjectDelta Delta;

	switch (Msg.DeltaType)
	{
		case 3:
			SWGTangibleDeltaParser::ParseDelta3(Packet, Delta, Msg.UpdateCount);

			if (USWGTangibleComponent* TangibleComponent = Actor.GetComponentByClass<USWGTangibleComponent>())
			{
				TangibleComponent->ApplyDelta3(Delta);
			}
			if (USWGConditionComponent* ConditionComponent = Actor.GetComponentByClass<USWGConditionComponent>())
			{
				ConditionComponent->ApplyDelta3(Delta);
			}
			break;

		case 6:
			SWGTangibleDeltaParser::ParseDelta6(Packet, Delta, Msg.UpdateCount);

			if (USWGDefenderComponent* DefenderComponent = Actor.GetComponentByClass<USWGDefenderComponent>())
			{
				DefenderComponent->ApplyDelta6(Delta);
			}
			break;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGTangibleDeltaHandler: no TANO delta dispatch for slot %d"), Msg.DeltaType);
			break;
	}

	return true;
}

REGISTER_SWG_DELTA_HANDLER(FSWGTangibleDeltaHandler,
	ESWGObjectType::TANO, ESWGObjectType::WEAO)
