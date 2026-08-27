#include "BaselineHandlers/SWGTangibleBaselineHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Object/TangibleObjectBaseline.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"

bool FSWGTangibleBaselineHandler::CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const
{
	// the filer on the message type should be enough, this may need to be updated in the future
	return true;
}

bool FSWGTangibleBaselineHandler::HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args)
{
	FSWGPacket Packet = Msg.AsPayloadPacket();
	FTangibleObjectBaseline Baseline;

	switch (Msg.BaselineType)
	{
		case 3:
			SWGTangibleBaselineParser::ParseBase3(Packet, Baseline);

			if (USWGTangibleComponent* TangibleComponent = Actor.GetComponentByClass<USWGTangibleComponent>())
			{
				TangibleComponent->ApplyBase3(Baseline);
			}
			if (USWGConditionComponent* ConditionComponent = Actor.GetComponentByClass<USWGConditionComponent>())
			{
				ConditionComponent->ApplyBase3(Baseline);
			}
			break;

		case 6:
			SWGTangibleBaselineParser::ParseBase6(Packet, Baseline);

			if (USWGDefenderComponent* DefenderComponent = Actor.GetComponentByClass<USWGDefenderComponent>())
			{
				DefenderComponent->ApplyBase6(Baseline);
			}
			break;

		default:
			UE_LOG(LogTemp, Verbose, TEXT("FSWGTangibleBaselineHandler: no TANO baseline dispatch for slot %d"), Msg.BaselineType);
			break;
	}

	return true;
}

REGISTER_SWG_BASELINE_HANDLER(FSWGTangibleBaselineHandler,
	ESWGObjectType::TANO, ESWGObjectType::WEAO)
